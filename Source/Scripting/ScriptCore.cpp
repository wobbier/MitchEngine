#include "PCH.h"
#include "ScriptCore.h"
#include "Components/Transform.h"
#include <mono/jit/jit.h>
#include <mono/metadata/assembly.h>
#include <mono/metadata/debug-helpers.h>
#include <mono/metadata/attrdefs.h>

#include "Path.h"
#include "imgui.h"
#include "File.h"
#include "Utils/PlatformUtils.h"
#include "MonoUtils.h"


struct ScriptData
{
    MonoDomain* RootDomain = nullptr;
    MonoDomain* AppDomain = nullptr;
    MonoAssembly* assembly = nullptr;
    MonoImage* assemblyImage = nullptr;
};
static ScriptData sScriptData;

static void NativeLog( MonoString* inString, int number )
{
    char* str = mono_string_to_utf8( inString );

    std::cout << str << std::to_string( number ) << std::endl;

    mono_free( str );
}

static void NativeLog_Vector( Vector3* inVector, Vector3* outVec )
{
    Vector3 vec = *inVector;
    std::cout << "CPP: " << std::to_string( vec.x ) << ", " << std::to_string( vec.y ) << ", " << std::to_string( vec.z ) << std::endl;
    inVector->Normalize();
    *outVec = *inVector;
}

static float Native_VectorLength( Vector3* inVector )
{
    return inVector->Length();
}

ScriptClass::ScriptClass( const std::string& inNameSpace, const std::string& inName )
{
    Class = mono_class_from_name( sScriptData.assemblyImage, inNameSpace.c_str(), inName.c_str() );
}


MonoObject* ScriptClass::Instantiate()
{
    ClassObject = mono_object_new( sScriptData.RootDomain, Class );

    if ( !ClassObject )
    {
        YIKES( "mono_object_new failed" );
        return nullptr;
    }

    mono_runtime_object_init( ClassObject );
    return ClassObject;
}

MonoMethod* ScriptClass::GetMethod( const std::string& inFuncName, int params ) const
{
    MonoMethod* method = mono_class_get_method_from_name( Class, inFuncName.c_str(), params );

    if ( method == nullptr )
    {
        YIKES( "mono_class_get_method_from_name failed" );
        return nullptr;
    }
    return method;
}

void ScriptClass::InvokeMethod( MonoMethod* inMethod, void** inParams )
{
    MonoObject* exception = nullptr;

    mono_runtime_invoke( inMethod, ClassObject, inParams, &exception );
}

void ScriptClass::InvokeFull( const std::string& inFuncName, int inParamCount /*= 0*/, void** inParams /*= nullptr */ )
{
    MonoMethod* method = GetMethod( inFuncName, inParamCount );
    InvokeMethod( method, inParams );
}

ScriptCore::ScriptCore() : Base( ComponentFilter().Requires<Transform>() )
{
    SetIsSerializable( false );
}

ScriptCore::~ScriptCore()
{
}


void ScriptCore::Init()
{
    mono_set_assemblies_path( MONO_PATH );

    mono_set_dirs( MONO_HOME "/lib", MONO_HOME "/etc" );

    sScriptData.RootDomain = mono_jit_init( "MitchEngineMonoRuntime" );
    if ( !sScriptData.RootDomain )
    {
        YIKES( "mono_jit_init failed" );
    }

    sScriptData.AppDomain = mono_domain_create_appdomain( "MEScriptRuntime", nullptr );
    mono_domain_set( sScriptData.AppDomain, true );

    // todo: fix path
    Path path( ".build/editor_debug/ScriptCore.dll" );
    LoadAssembly( path );

    // Register funcs
    RegisterFunctions();

    // Tests
    Tests();
}
void ScriptCore::Update( const UpdateContext& inUpdateContext )
{
}

void ScriptCore::LateUpdate( const UpdateContext& inUpdateContext )
{

}

void ScriptCore::OnEntityAdded( Entity& NewEntity )
{

}

void ScriptCore::OnEntityRemoved( Entity& InEntity )
{

}

void ScriptCore::RegisterFunctions()
{
    mono_add_internal_call( "TestScript::CppFunc", CppFunc );
    mono_add_internal_call( "TestScript::NativeLog", NativeLog );
    mono_add_internal_call( "TestScript::NativeLog_Vector", NativeLog_Vector );
    mono_add_internal_call( "TestScript::Native_VectorLength", Native_VectorLength );
}

void ScriptCore::CppFunc()
{
    std::cout << "C++ FUNC" << std::endl;
}

void ScriptCore::Tests()
{
    CacheAssemblyTypes( sScriptData.assembly );

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

    if ( floatFieldAccessibility & (uint8_t)MonoUtils::Accessibility::Public )
    {
        std::cout << "PUBLIC: MyPublicFloatVar" << std::endl;
    }

    // string field accessibility
    MonoClassField* nameField = mono_class_get_field_from_name( testingClass, "m_Name" );
    uint8_t nameFieldAccessibility = MonoUtils::GetFieldAccessibility( nameField );

    if ( nameFieldAccessibility & (uint8_t)MonoUtils::Accessibility::Private )
    {
        std::cout << "PRIVATE: m_Name" << std::endl;
    }

    // string property accessibility
    MonoProperty* nameProperty = mono_class_get_property_from_name( testingClass, "Name" );
    uint8_t namePropertyAccessibility = MonoUtils::GetPropertyAccessibility( nameProperty );

    if ( namePropertyAccessibility & (uint8_t)MonoUtils::Accessibility::Public )
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
}


bool ScriptCore::LoadAssembly( const Path& assemblyPath )
{
    uint32_t fileSize = 0;
    char* fileData = PlatformUtils::ReadBytes( assemblyPath, &fileSize );

    MonoImageOpenStatus status;
    MonoImage* image = mono_image_open_from_data_full( fileData, fileSize, 1, &status, 0 );

    delete[] fileData;

    if ( status != MONO_IMAGE_OK )
    {
        const char* errorMessage = mono_image_strerror( status );
        YIKES( errorMessage );
        return nullptr;
    }

    sScriptData.assembly = mono_assembly_load_from_full( image, assemblyPath.FullPath.c_str(), &status, 0 );

    mono_image_close( image );

    if ( !sScriptData.assembly )
    {
        YIKES( "mono_assembly_load_from_full failed" );
        return false;
    }

    sScriptData.assemblyImage = mono_assembly_get_image( sScriptData.assembly );
    if ( !sScriptData.assemblyImage )
    {
        YIKES( "mono_assembly_get_image failed" );
        return false;
    }

    return true;
}

void ScriptCore::CacheAssemblyTypes( MonoAssembly* assembly )
{
    MonoImage* image = mono_assembly_get_image( assembly );
    const MonoTableInfo* typeDefinitionsTable = mono_image_get_table_info( image, MONO_TABLE_TYPEDEF );
    int32_t numTypes = mono_table_info_get_rows( typeDefinitionsTable );

    for ( int32_t i = 0; i < numTypes; i++ )
    {
        uint32_t cols[MONO_TYPEDEF_SIZE];
        mono_metadata_decode_row( typeDefinitionsTable, i, cols, MONO_TYPEDEF_SIZE );

        const char* nameSpace = mono_metadata_string_heap( image, cols[MONO_TYPEDEF_NAMESPACE] );
        const char* name = mono_metadata_string_heap( image, cols[MONO_TYPEDEF_NAME] );

        LoadedClassInfo loadedClass;
        loadedClass.Namespace = std::string( nameSpace );
        loadedClass.Name = std::string( name );
        LoadedClasses.push_back( std::move( loadedClass ) );
    }
}

void ScriptCore::OnEditorInspect()
{
    float value;
    mono_field_get_value( testClassInstance.ClassObject, floatField, &value );
    ImGui::DragFloat( "C# Float", &value );
    mono_field_set_value( testClassInstance.ClassObject, floatField, &value );

    testClassInstance.InvokeFull("PrintFloatVar");

    for ( auto& it : LoadedClasses )
    {
        ImGui::Text( "%s %s", it.Namespace.c_str(), it.Name.c_str() );
    }
}
