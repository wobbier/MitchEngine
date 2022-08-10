#pragma once
#include "ECS/Core.h"
#include "MonoFwd.h"
#include <string>
#include "Path.h"

class ScriptClass
{
public:
    ScriptClass() = default;
    ScriptClass( const std::string& inNameSpace, const std::string& inName );

    // maybe have a create instance that returns ScriptClass
    MonoObject* Instantiate();

    MonoMethod* GetMethod( const std::string& inFuncName, int inParamCount ) const;
    void InvokeMethod( MonoMethod* inMethod, void** inParams );
    void InvokeFull( const std::string& inFuncName, int inParamCount = 0, void** inParams = nullptr );

    MonoClass* Class = nullptr;
    MonoObject* ClassObject = nullptr;
};


class ScriptCore
    : public Core<ScriptCore>
{
    struct LoadedClassInfo
    {
        std::string Namespace;
        std::string Name;
    };

public:
    ScriptCore();
    ~ScriptCore();

    // Separate init from construction code.
    void Init() final;

    static void CppFunc();
    // Each core must update each loop
    void Update( const UpdateContext& inUpdateContext ) final;
    void LateUpdate( const UpdateContext& inUpdateContext ) final;

    void OnEntityAdded( Entity& NewEntity ) final;
    void OnEntityRemoved( Entity& InEntity ) final;

    void OnEditorInspect() final;

    void RegisterFunctions();
    void Tests();

private:
    bool LoadAssembly( const Path& assemblyPath );
    void CacheAssemblyTypes( MonoAssembly* assembly );

private:
    ScriptClass testClassInstance;
    MonoClassField* floatField = nullptr;
    std::vector<LoadedClassInfo> LoadedClasses;
};

ME_REGISTER_CORE( ScriptCore )