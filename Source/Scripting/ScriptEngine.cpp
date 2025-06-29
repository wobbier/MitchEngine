#include "PCH.h"
#include "ScriptEngine.h"

#if USING( ME_SCRIPTING )

#include <mono/jit/jit.h>
#include <mono/metadata/assembly.h>
#include <mono/metadata/debug-helpers.h>
#include <mono/metadata/attrdefs.h>
#include "Utils/PlatformUtils.h"
#include "MonoUtils.h"
#include <mono/metadata/mono-debug.h>
#include <mono/metadata/threads.h>
#include "ECS/Entity.h"
#include "Engine/Engine.h"
#include "Cores/SceneCore.h"

#include <mono/metadata/assembly.h>
#include <mono/metadata/class.h>
#include <mono/metadata/image.h>
#include <mono/metadata/debug-helpers.h>
#include <mono/metadata/tokentype.h>

#include "Bindings/ImGui.bindings.h"
#include "Bindings/World.bindings.h"
#include "Bindings/Log.bindings.h"

//// forward declare internal Mono debugger agent functions
//extern "C" {
//    void mono_debugger_agent_send_assembly_load( MonoAssembly* assembly );
//    void mono_debugger_agent_send_type_load( MonoClass* klass );
//    void mono_debugger_agent_send_method( MonoMethod* method );
//}


ScriptEngine::ScriptData ScriptEngine::sScriptData;

std::vector<ScriptEngine::LoadedClassInfo> ScriptEngine::LoadedClasses;

std::vector<ScriptEngine::LoadedClassInfo> ScriptEngine::LoadedEntityScripts;

std::unordered_map<EntityID, uint32_t> ScriptEngine::entityInstanceCache;


//void ResendDebugInfo( MonoAssembly* monoAssembly )
//{
//    //for( MonoAssembly* asm : allLoadedAssemblies )
//    {
//        mono_debugger_agent_send_assembly_load( monoAssembly );
//
//        MonoImage* img = mono_assembly_get_image( monoAssembly );
//        int typeCount = mono_image_get_table_rows( img, MONO_TABLE_TYPEDEF );
//
//        for( int i = 1; i < typeCount; ++i )
//        {
//            MonoClass* klass = mono_class_get( img, ( i + 1 ) | MONO_TOKEN_TYPE_DEF );
//            if( !klass ) continue;
//
//            mono_debugger_agent_send_type_load( klass );
//
//            void* iter = nullptr;
//            while( MonoMethod* method = mono_class_get_methods( klass, &iter ) )
//            {
//                mono_debugger_agent_send_method( method );
//            }
//        }
//    }
//}
//
//void ResendDebugInfo()
//{
//    ResendDebugInfo( ScriptEngine::sScriptData.CoreAssembly );
//    ResendDebugInfo( ScriptEngine::sScriptData.AppAssembly );
//}



MonoObject* ScriptEngine::ReturnEntityID( const EntityHandle& inEntity )
{
    if( !inEntity )
    {
        return nullptr;
    }

    EntityID id = inEntity.GetID();
    auto it = ScriptEngine::entityInstanceCache.find( id );
    if( it != ScriptEngine::entityInstanceCache.end() )
    {
        MonoObject* entityObj = mono_gchandle_get_target( it->second );
        if( entityObj == nullptr )
        {
            YIKES( "MonoObject reference was lost (collected by GC)" );
            return nullptr;
        }
        return entityObj;
    }

    MonoClass* entityClass = mono_class_from_name( ScriptEngine::sScriptData.CoreAssemblyImage, "", "Entity" );
    MonoObject* entityObj = mono_object_new( mono_domain_get(), entityClass );

    MonoMethod* ctor = mono_class_get_method_from_name( entityClass, ".ctor", 1 );
    void* args[] = { &id };
    mono_runtime_invoke( ctor, entityObj, args, nullptr );
    // Create a GC handle to ensure object isn't collected
    uint32_t gcHandle = mono_gchandle_new( entityObj, /*pinned=*/false );
    ScriptEngine::entityInstanceCache[id] = gcHandle;

    return entityObj;
}


ScriptClass::ScriptClass( const std::string& inNameSpace, const std::string& inName, bool isCore )
{
#if USING( ME_DEBUG )
    Name = inName;
    Namespace = inNameSpace;
#endif

    Class = mono_class_from_name( isCore ? ScriptEngine::sScriptData.CoreAssemblyImage : ScriptEngine::sScriptData.AppAssemblyImage, inNameSpace.c_str(), inName.c_str() );
}


MonoObject* ScriptClass::Instantiate()
{
    MonoObject* instance = mono_object_new( ScriptEngine::sScriptData.RootDomain, Class );

    if( !instance )
    {
        YIKES( "mono_object_new failed" );
        return nullptr;
    }

    return instance;
}

MonoMethod* ScriptClass::GetMethod( const std::string& inFuncName, int params ) const
{
    MonoMethod* method = mono_class_get_method_from_name( Class, inFuncName.c_str(), params );

    if( method == nullptr )
    {
        YIKES( "mono_class_get_method_from_name failed" );
        return nullptr;
    }
    return method;
}

void ScriptClass::InvokeMethod( MonoObject* inInstance, MonoMethod* inMethod, void** inParams )
{
    MonoObject* exception = nullptr;
    ME_ASSERT_MSG( inInstance != nullptr, "ScriptClass::InvokeMethod instance is bad!!" );
    mono_runtime_invoke( inMethod, inInstance, inParams, &exception );
}


void ScriptEngine::Init()
{
    if( sScriptData.RootDomain )
    {
        return;
    }

    InitMono();

    // Register funcs
    RegisterFunctions();

    Path path( "ScriptCore.dll" );
    Path appPath( "Game.Script.dll" );
    // todo: fix path
#if USING( ME_EDITOR )
#if USING( ME_RELEASE )
    if( !path.Exists )
    {
        path = Path( ".build/editor_release/ScriptCore.dll" );
    }
    if( !appPath.Exists )
    {
        appPath = Path( ".build/editor_release/Game.Script.dll" );
    }
#else
    if( !path.Exists )
    {
        path = Path( ".build/editor_debug/ScriptCore.dll" );
    }
    if( !appPath.Exists )
    {
        appPath = Path( ".build/editor_debug/Game.Script.dll" );
    }
#endif
#else
    if( !path.Exists )
    {
        path = Path( ".build/editor_debug/ScriptCore.dll" );
    }
    if( !appPath.Exists )
    {
        appPath = Path( ".build/editor_debug/Game.Script.dll" );
    }
#endif // else

    LoadAssembly( path );
    LoadAppAssembly( appPath );

    CacheAssemblyTypes();

    sScriptData.entityClass = ScriptClass( "", "Entity", true );

    // Tests
    Tests();
}

void ScriptEngine::InitDebug()
{
    if( sScriptData.EnableDebugging )
    {
        mono_debug_domain_create( sScriptData.RootDomain );
    }
}

void ScriptEngine::InitMono()
{
    mono_set_assemblies_path( MONO_PATH );
    BRUH_FMT( "Mono version: %s", mono_get_runtime_build_info() );


    if( sScriptData.EnableDebugging )
    {
        const char* argv[2] = {
            "--debugger-agent=transport=dt_socket,address=127.0.0.1:55555,server=y,suspend=n,loglevel=3,logfile=MonoDebugger.log",
            "--soft-breakpoints",
        };
        mono_jit_parse_options( 2, (char**)argv );
        mono_debug_init( MONO_DEBUG_FORMAT_MONO );
    }

    //mono_set_dirs( MONO_HOME "/lib", MONO_HOME "/etc" );
    sScriptData.RootDomain = mono_jit_init_version( "MEMonoRuntime", "v4.8" );
    if( !sScriptData.RootDomain )
    {
        YIKES( "mono_jit_init failed" );
    }
    InitDebug();

    mono_thread_set_main( mono_thread_current() );
}

void ScriptEngine::RegisterFunctions()
{
    Register_ImGuiBindings();
    Register_WorldBindings();
    Register_LogBindings();
}


void ScriptEngine::Tests()
{
#if 0
    testClassInstance = ScriptClass( "", "TestScript" );
    testClassInstance.Instantiate();

    testClassInstance.InvokeFull( "PrintFloatVar" );

    float increment = 5.0f;
    void* params[] =
    {
        &increment
    };
    testClassInstance.InvokeFull( "IncrementFloatVar", 1, params );

    testClassInstance.InvokeFull( "PrintFloatVar" );
    MonoClass* testingClass = testClassInstance.Class;

    // float property accessibility
    floatField = mono_class_get_field_from_name( testingClass, "MyPublicFloatVar" );
    uint8_t floatFieldAccessibility = MonoUtils::GetFieldAccessibility( floatField );

    if( floatFieldAccessibility & MonoUtils::Accessibility::Public )
    {
        std::cout << "PUBLIC: MyPublicFloatVar" << std::endl;
    }

    // string field accessibility
    MonoClassField* nameField = mono_class_get_field_from_name( testingClass, "m_Name" );
    uint8_t nameFieldAccessibility = MonoUtils::GetFieldAccessibility( nameField );

    if( nameFieldAccessibility & MonoUtils::Accessibility::Private )
    {
        std::cout << "PRIVATE: m_Name" << std::endl;
    }

    // string property accessibility
    MonoProperty* nameProperty = mono_class_get_property_from_name( testingClass, "Name" );
    uint8_t namePropertyAccessibility = MonoUtils::GetPropertyAccessibility( nameProperty );

    if( namePropertyAccessibility & MonoUtils::Accessibility::Public )
    {
        std::cout << "PUBLIC: Name" << std::endl;
    }

    // float field
    float value;
    mono_field_get_value( testClassInstance.ClassObject, floatField, &value );

    value += 10.0f;
    mono_field_set_value( testClassInstance.ClassObject, floatField, &value );

    // string property
    MonoString* nameValue = (MonoString*)mono_property_get_value( nameProperty, testClassInstance.ClassObject, nullptr, nullptr );
    std::string nameStr = MonoUtils::MonoStringToUTF8( nameValue );

    nameStr += ", World!";
    nameValue = mono_string_new( sScriptData.RootDomain, nameStr.c_str() );
    mono_property_set_value( nameProperty, testClassInstance.ClassObject, (void**)&nameValue, nullptr );


    // float property
    MonoProperty* floatProperty = mono_class_get_property_from_name( testingClass, "TestFloatProperty" );

    MonoObject* floatValueObj = mono_property_get_value( floatProperty, testClassInstance.ClassObject, nullptr, nullptr );
    float floatValue = *(float*)mono_object_unbox( floatValueObj );
    floatValue += 10.0f;

    void* data[] = { &floatValue };
    mono_property_set_value( nameProperty, testClassInstance.ClassObject, data, nullptr );
#endif
}


ScriptClass& ScriptEngine::GetEntityClass( const std::string& name )
{
    return sScriptData.EntityClasses.at( name );
}

const ScriptFieldMap& ScriptEngine::GetScriptFieldMap( Entity ent )
{
    return sScriptData.EntityScriptFields.at( ent.GetId().Value() );
}

MonoImage* ScriptEngine::GetCoreImage()
{
    return sScriptData.CoreAssemblyImage;
}

SharedPtr<ScriptInstance> ScriptEngine::CreateScriptInstance( ScriptClass& script, EntityHandle entity )
{
    sScriptData.EntityInstances[entity->GetId().Value()] = MakeShared<ScriptInstance>( script, entity );
    return sScriptData.EntityInstances[entity->GetId().Value()];
}

bool ScriptEngine::LoadAssembly( const Path& assemblyPath )
{
    char name[30] = "MEScriptRuntime\n";
    sScriptData.AppDomain = mono_domain_create_appdomain( &name[0], nullptr );
    mono_domain_set( sScriptData.AppDomain, true );

    sScriptData.CoreAssembly = MonoUtils::LoadMonoAssembly( assemblyPath, sScriptData.EnableDebugging );
    sScriptData.CoreAssemblyFilePath = assemblyPath;

    sScriptData.CoreAssemblyImage = mono_assembly_get_image( sScriptData.CoreAssembly );

    if( !sScriptData.CoreAssemblyImage )
    {
        YIKES( "mono_assembly_get_image failed" );
        return false;
    }

    return true;
}

void ScriptEngine::ReloadAssembly()
{
    mono_domain_set( mono_get_root_domain(), false );
    mono_domain_unload( sScriptData.AppDomain );

    LoadAssembly( sScriptData.CoreAssemblyFilePath );
    LoadAppAssembly( sScriptData.AppAssemblyFilePath );
    CacheAssemblyTypes();

    sScriptData.entityClass = ScriptClass( "", "Entity", true );
}

bool ScriptEngine::LoadAppAssembly( const Path& assemblyPath )
{
    sScriptData.AppAssembly = MonoUtils::LoadMonoAssembly( assemblyPath, sScriptData.EnableDebugging );
    sScriptData.AppAssemblyFilePath = assemblyPath;

    sScriptData.AppAssemblyImage = mono_assembly_get_image( sScriptData.AppAssembly );
    if( !sScriptData.AppAssemblyImage )
    {
        YIKES( "mono_assembly_get_image failed" );
        return false;
    }

    //#TODO: Listen for dll changes

    return true;
}

void ScriptEngine::CacheAssemblyTypes()
{
    sScriptData.EntityClasses.clear();

    const MonoTableInfo* typeDefinitionsTable = mono_image_get_table_info( sScriptData.AppAssemblyImage, MONO_TABLE_TYPEDEF );
    int32_t numTypes = mono_table_info_get_rows( typeDefinitionsTable );

    MonoClass* monoBase = mono_class_from_name( sScriptData.CoreAssemblyImage, "", "MEObject" );
    for( int32_t i = 0; i < numTypes; i++ )
    {
        uint32_t cols[MONO_TYPEDEF_SIZE];
        mono_metadata_decode_row( typeDefinitionsTable, i, cols, MONO_TYPEDEF_SIZE );

        std::string nameSpace = mono_metadata_string_heap( sScriptData.AppAssemblyImage, cols[MONO_TYPEDEF_NAMESPACE] );
        std::string name = mono_metadata_string_heap( sScriptData.AppAssemblyImage, cols[MONO_TYPEDEF_NAME] );
        std::string fullName;
        if( !nameSpace.empty() )
        {
            fullName = std::string( nameSpace + "." + name );
        }
        else
        {
            fullName = name;
        }

        LoadedClassInfo loadedClass;
        loadedClass.Namespace = nameSpace;
        loadedClass.Name = name;

        MonoClass* monoClass = mono_class_from_name( sScriptData.AppAssemblyImage, nameSpace.c_str(), name.c_str() );
        //if( monoClass == monoBase )
        //    continue;

        bool isEntity = mono_class_is_subclass_of( monoClass, monoBase, false );
        if( isEntity )
        {
            sScriptData.EntityClasses[fullName] = ScriptClass( loadedClass.Namespace, loadedClass.Name );
            ScriptClass& scriptClass = sScriptData.EntityClasses[fullName];
            int fieldCount = mono_class_num_fields( monoClass );
            void* it = nullptr;
            while( MonoClassField* field = mono_class_get_fields( monoClass, &it ) )
            {
                const char* fieldName = mono_field_get_name( field );
                uint32_t flags = mono_field_get_flags( field );
                if( flags & MONO_FIELD_ATTR_PUBLIC )
                {
                    MonoType* type = mono_field_get_type( field );
                    MonoUtils::ScriptFieldType fieldType = MonoUtils::MonoTypeToScriptFieldType( type );
                    //BRUH_FMT( "%s, %i", MonoUtils::ScriptFieldTypeToString( fieldType ).c_str(), fieldType );
                    scriptClass.m_fields[fieldName] = { fieldType, fieldName, field };
                }
            }
            LoadedEntityScripts.push_back( loadedClass );
        }
        LoadedClasses.push_back( loadedClass );
    }
}

MonoClass* ScriptInstance::GetMonoClass() const
{
    return mono_object_get_class( GetMonoObjectInstance() );
}

void ScriptInstance::OnCreate()
{
    MonoClass* klass = GetMonoClass();
    MonoClassField* parentField = nullptr;

    while( klass != nullptr )
    {
        parentField = mono_class_get_field_from_name( klass, "_parent" );
        if( parentField != nullptr )
            break;

        klass = mono_class_get_parent( klass ); // walk up to base class
    }

    if( parentField )
    {
        MonoObject* managedEntity = ScriptEngine::ReturnEntityID( Owner );
        if( managedEntity == nullptr )
        {
            YIKES( "MonoObject reference was lost (collected by GC)" );
        }

        if( managedEntity )
            mono_field_set_value( GetMonoObject(), parentField, managedEntity );
    }

    if( OnCreateMethod )
    {
        ScriptRef.InvokeMethod( GetMonoObjectInstance(), OnCreateMethod, nullptr );
    }
}

MonoObject* ScriptInstance::GetMonoObjectInstance() const
{
    return mono_gchandle_get_target( GCHandle );
}


void ScriptInstance::Init( int numParams /*= 0*/, void** params /*= nullptr*/ )
{
    if( numParams <= 0 )
    {
        mono_runtime_object_init( GetMonoObjectInstance() );
    }
    else
    {
        auto method = ScriptEngine::sScriptData.entityClass.GetMethod( ".ctor", numParams );
        ScriptRef.InvokeMethod( GetMonoObjectInstance(), method, params );
    }
}

bool ScriptInstance::GetFieldValueInternal( const std::string& name, void* outValue )
{
    const auto& fields = GetScriptClass().m_fields;
    auto it = fields.find( name );
    if( it == fields.end() )
        return false;

    mono_field_get_value( GetMonoObjectInstance(), it->second.Field, outValue );
    return true;
}

bool ScriptInstance::SetFieldValueInternal( const std::string& name, void* inValue )
{
    const auto& fields = GetScriptClass().m_fields;
    auto it = fields.find( name );
    if( it == fields.end() )
        return false;

    mono_field_set_value( GetMonoObjectInstance(), it->second.Field, inValue );
    return true;
}

#endif
