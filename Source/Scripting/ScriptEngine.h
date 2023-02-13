#pragma once
#include "Scripting/MonoFwd.h"
#include "Path.h"
#include "ECS/EntityHandle.h"
#include "Engine/World.h"
#include "MonoUtils.h"


struct ScriptField
{
    MonoUtils::ScriptFieldType Type;
    std::string Name;

    MonoClassField* Field;
};


class ScriptClass
{
    friend class ScriptComponent;
public:
    ScriptClass() = default;
    ScriptClass( const std::string& inNameSpace, const std::string& inName );

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
        void* entID = &inOwner.GetID();
        Init( 1, &entID );
    }

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
    inline static char sFieldValueBuffer[8];
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
        MonoAssembly* assembly = nullptr;
        MonoImage* assemblyImage = nullptr;

        MonoAssembly* appAssembly = nullptr;
        MonoImage* appAssemblyImage = nullptr;

        WeakPtr<World> worldPtr;

        ScriptClass entityClass;

        std::unordered_map<std::string, ScriptClass> ClassInfo;
    };

    static void Init();

    static void RegisterFunctions();
    static void Tests();

    static MonoImage* GetCoreImage();
private:
    static bool LoadAssembly( const Path& assemblyPath );
    static bool LoadAppAssembly( const Path& assemblyPath );
    static void CacheAssemblyTypes();

public:
    static ScriptData sScriptData;
    static ScriptClass testClassInstance;
    static MonoClassField* floatField;
    static std::vector<LoadedClassInfo> LoadedClasses;
    static std::vector<LoadedClassInfo> LoadedEntityScripts;
};