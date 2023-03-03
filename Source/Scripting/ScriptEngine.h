#pragma once
#include "Scripting/MonoFwd.h"
#include "Path.h"
#include "ECS/EntityHandle.h"
#include "Engine/World.h"
#include "MonoUtils.h"
#include "ECS/Entity.h"


struct ScriptField
{
    MonoUtils::ScriptFieldType Type;
    std::string Name;

    MonoClassField* Field = nullptr;
};


struct ScriptFieldInstance
{
    ScriptField Instance;

    const char* GetBuffer() const
    {
        return Buffer;
    }

    template <typename T>
    T GetValue()
    {
        static_assert( sizeof( T ) <= 32, "Field type is too large for buffer." );
        return *(T*)Buffer;
    }

    template <typename T>
    void SetValue( T value )
    {
        static_assert( sizeof( T ) <= 32, "Field type is too large for buffer." );
        memcpy( Buffer, value );
    }

private:
    char Buffer[32];
    friend class ScriptComponent;
};

using ScriptFieldMap = std::unordered_map<std::string, ScriptFieldInstance>;

class ScriptClass
{
    friend class ScriptComponent;
public:
    ScriptClass() = default;
    ScriptClass( const std::string& inNameSpace, const std::string& inName, bool isCore = false );

    // maybe have a create instance that returns ScriptClass
    MonoObject* Instantiate();

    MonoMethod* GetMethod( const std::string& inFuncName, int inParamCount ) const;
    void InvokeMethod( MonoObject* inInstance, MonoMethod* inMethod, void** inParams );

    std::unordered_map<std::string, ScriptField> m_fields;

    MonoClass* Class = nullptr;
};


class ScriptInstance
{
public:
    ScriptInstance( ScriptClass& inClass, EntityHandle inOwner )
        : ScriptRef( inClass )
        , Owner( inOwner )
    {
        Instance = inClass.Instantiate();
        OnCreateMethod = inClass.GetMethod( "OnCreate", 0 );
        OnUpdateMethod = inClass.GetMethod( "OnUpdate", 1 );
        void* entID = &inOwner;
        Init( 1, &entID );
        //Owner = EntityHandle( inOwner );
    }

    //explicit ScriptInstance( ScriptInstance& inClass )
    //    : ScriptRef( inClass.ScriptRef )
    //    , Owner( inClass.Owner )
    //    , Instance( inClass.Instance )
    //    , OnCreateMethod( inClass.OnCreateMethod )
    //    , OnUpdateMethod( inClass.OnUpdateMethod )
    //{
    //}

    void OnCreate()
    {
        ScriptRef.InvokeMethod( Instance, OnCreateMethod, nullptr );
    }

    void OnUpdate( float deltaTime )
    {
        void* param = &deltaTime;
        ScriptRef.InvokeMethod( Instance, OnUpdateMethod, &param );
    }

    template <typename T>
    T GetFieldValue( const std::string& name );

    template <typename T>
    void SetFieldValue( const std::string& name, T& value );

    MonoObject* Instance = nullptr;
    MonoMethod* OnCreateMethod = nullptr;
    MonoMethod* OnUpdateMethod = nullptr;

    const ScriptClass& GetScriptClass() const
    {
        return ScriptRef;
    }

private:
    void Init( int numParams = 0, void** params = nullptr );
    bool GetFieldValueInternal( const std::string& name, void* outValue );

    bool SetFieldValueInternal( const std::string& name, void* inValue );

    ScriptClass& ScriptRef;
    EntityHandle Owner;
    inline static char sFieldValueBuffer[32];
    friend class ScriptComponent;
};

template <typename T>
T ScriptInstance::GetFieldValue( const std::string& name )
{
    void* value = nullptr;
    if ( !GetFieldValueInternal( name, sFieldValueBuffer ) )
    {
        return T();
    }

    return *( (T*)sFieldValueBuffer );
}

template <typename T>
void ScriptInstance::SetFieldValue( const std::string& name, T& value )
{
    SetFieldValueInternal( name, &value );
}

class ScriptEngine
{
public:
    struct LoadedClassInfo
    {
        std::string Namespace;
        std::string Name;
    };

    struct ScriptData
    {
        MonoDomain* RootDomain = nullptr;
        MonoDomain* AppDomain = nullptr;

        MonoAssembly* CoreAssembly = nullptr;
        MonoImage* CoreAssemblyImage = nullptr;
        Path CoreAssemblyFilePath;

        MonoAssembly* AppAssembly = nullptr;
        MonoImage* AppAssemblyImage = nullptr;
        Path AppAssemblyFilePath;

        bool EnableDebugging = true;

        WeakPtr<World> worldPtr;

        ScriptClass entityClass;

        std::unordered_map<std::string, ScriptClass> EntityClasses;

        std::unordered_map<EntityID::IntType, SharedPtr<ScriptInstance>> EntityInstances;
        std::unordered_map<EntityID::IntType, ScriptFieldMap> EntityScriptFields;
    };

    static void Init();

    static void RegisterFunctions();
    static void Tests();

    static ScriptClass& GetEntityClass( const std::string& name );
    static const ScriptFieldMap& GetScriptFieldMap( Entity ent );

    static MonoImage* GetCoreImage();

    static SharedPtr<ScriptInstance> CreateScriptInstance( ScriptClass& script, EntityHandle entity );
private:
    static void InitMono();
    static bool LoadAssembly( const Path& assemblyPath );
    static void ReloadAssembly();
    static bool LoadAppAssembly( const Path& assemblyPath );
    static void CacheAssemblyTypes();

public:
    static ScriptData sScriptData;
    static ScriptClass testClassInstance;
    static MonoClassField* floatField;

    // Parsed class
    static std::vector<LoadedClassInfo> LoadedClasses;

    // Successful class loaded
    static std::vector<LoadedClassInfo> LoadedEntityScripts;
};