#include "PCH.h"
#include "ScriptEngine.h"
#include "ScriptHost.h"

#if USING( ME_SCRIPTING )

#include <ctime>   // Eng_GetTime: clock() / CLOCKS_PER_SEC
#include "Generated/ScriptEngineAPI.generated.h"

// per-domain binding registrars
#include "Bindings/BindingContext.h"
#include "Bindings/Entity.bindings.h"
#include "Bindings/ImGui.bindings.h"
#include "Bindings/Components/Transform.bindings.h"
#include "Bindings/Components/Camera.bindings.h"
#include "Bindings/Components/BasicUIView.bindings.h"
#include "Bindings/Systems/World.bindings.h"
#include "Bindings/Systems/Input.bindings.h"

#if USING( ME_EDITOR )
#define build_prefix "editor_"
#define build_platform ""
#else
#define build_prefix "game_"

#if USING( ME_PLATFORM_WINDOWS )
#define build_platform "win64_"
#elif USING( ME_PLATFORM_LINUX )
#define build_platform "linux_"
#elif USING( ME_PLATFORM_MACOS )
#define build_platform "macos_"
#endif

#endif


#if USING( ME_DEBUG )
#define build_postfix "debug"
#else
#define build_postfix "release"
#endif


static ScriptHost gScriptHost;


static void Eng_Log( const uint8_t* inMsg )
{
    DBG( "{}", reinterpret_cast<const char*>( inMsg ) );
}
static float Eng_GetTime()
{
    return static_cast<float>( clock() ) / CLOCKS_PER_SEC;
}


// script bindings
using FnLoadGameAssembly = int  ( * )( const uint8_t* );
using FnReloadGameAssembly = int  ( * )( const uint8_t* );
using FnGetScriptCount = int  ( * )( );
using FnGetScriptName = void ( * )( int, uint8_t*, int );
using FnGetFieldCount = int  ( * )( const uint8_t* );
using FnGetFieldInfo = void ( * )( const uint8_t*, int, uint8_t*, int, uint8_t*, int );
using FnGetFieldValue = void ( * )( int, int, uint8_t*, int );
using FnSetFieldValue = int  ( * )( int, int, const uint8_t* );
using FnGetInstanceCount = int  ( * )( );
using FnGetInstanceHandle = int  ( * )( int );
using FnGetInstanceTypeName = void ( * )( int, uint8_t*, int );
using FnCreateScript = int  ( * )( const uint8_t*, EntityID );
using FnScriptOnStart = void ( * )( int );
using FnScriptOnUpdate = void ( * )( int, float );
using FnScriptOnDestroy = void ( * )( int );
using FnGetMethodCount = int  ( * )( const uint8_t* );
using FnGetMethodName = void ( * )( const uint8_t*, int, uint8_t*, int );
using FnSetEngineAPI = int  ( * )( const ScriptEngineAPI*, int );
using FnRestoreScript = int  ( * )( const uint8_t*, int );
using FnOnEditorInspect = void  ( * )( int );
using FnGetFieldsJson = void ( * )( int, uint8_t*, int );
using FnSetFieldsJson = void ( * )( int, const uint8_t* );

struct ScriptAPI
{
    FnLoadGameAssembly    LoadGameAssembly = nullptr;
    FnReloadGameAssembly  ReloadGameAssembly = nullptr;
    FnGetScriptCount      GetScriptCount = nullptr;
    FnGetScriptName       GetScriptName = nullptr;
    FnGetFieldCount       GetFieldCount = nullptr;
    FnGetFieldInfo        GetFieldInfo = nullptr;
    FnGetFieldValue       GetFieldValue = nullptr;
    FnSetFieldValue       SetFieldValue = nullptr;
    FnGetInstanceCount    GetInstanceCount = nullptr;
    FnGetInstanceHandle   GetInstanceHandle = nullptr;
    FnGetInstanceTypeName GetInstanceTypeName = nullptr;
    FnCreateScript        CreateScript = nullptr;
    FnScriptOnStart       ScriptOnStart = nullptr;
    FnScriptOnUpdate      ScriptOnUpdate = nullptr;
    FnScriptOnDestroy     ScriptOnDestroy = nullptr;
    FnGetMethodCount      GetMethodCount = nullptr;
    FnGetMethodName       GetMethodName = nullptr;
    FnSetEngineAPI        SetEngineAPI = nullptr;
    FnRestoreScript       RestoreScript = nullptr;
    FnOnEditorInspect     ScriptOnEditorInspect = nullptr;
    FnGetFieldsJson       GetFieldsJson = nullptr;
    FnSetFieldsJson       SetFieldsJson = nullptr;

    bool IsValid() const
    {
        return LoadGameAssembly && SetEngineAPI
            && GetScriptCount && GetScriptName
            && CreateScript && ScriptOnStart && ScriptOnUpdate && ScriptOnDestroy;
    }
};

static ScriptAPI gDotnetAPI;

static bool LoadAPI( ScriptHost& inHost, const std::string& inCoreDll, ScriptAPI& outApi )
{
    const std::string bridgeType = "ScriptCore.ScriptBridge, ScriptCore";
    bool ok = true;
    ok &= inHost.LoadFunction( inCoreDll, bridgeType, "LoadGameAssembly", (void**)&outApi.LoadGameAssembly );
    ok &= inHost.LoadFunction( inCoreDll, bridgeType, "SetEngineAPI", (void**)&outApi.SetEngineAPI );
    ok &= inHost.LoadFunction( inCoreDll, bridgeType, "GetScriptCount", (void**)&outApi.GetScriptCount );
    ok &= inHost.LoadFunction( inCoreDll, bridgeType, "GetScriptName", (void**)&outApi.GetScriptName );
    ok &= inHost.LoadFunction( inCoreDll, bridgeType, "CreateScript", (void**)&outApi.CreateScript );
    ok &= inHost.LoadFunction( inCoreDll, bridgeType, "ScriptOnStart", (void**)&outApi.ScriptOnStart );
    ok &= inHost.LoadFunction( inCoreDll, bridgeType, "ScriptOnUpdate", (void**)&outApi.ScriptOnUpdate );
    ok &= inHost.LoadFunction( inCoreDll, bridgeType, "ScriptOnDestroy", (void**)&outApi.ScriptOnDestroy );
    ok &= inHost.LoadFunction( inCoreDll, bridgeType, "ScriptOnEditorInspect", (void**)&outApi.ScriptOnEditorInspect );
    ok &= inHost.LoadFunction( inCoreDll, bridgeType, "GetFieldsJson", (void**)&outApi.GetFieldsJson );
    ok &= inHost.LoadFunction( inCoreDll, bridgeType, "SetFieldsJson", (void**)&outApi.SetFieldsJson );
    //ok &= inHost.LoadFunction( inCoreDll, bridgeType, "ReloadGameAssembly",  (void**)&outApi.ReloadGameAssembly );
    //ok &= inHost.LoadFunction( inCoreDll, bridgeType, "GetFieldCount",       (void**)&outApi.GetFieldCount );
    //ok &= inHost.LoadFunction( inCoreDll, bridgeType, "GetFieldInfo",        (void**)&outApi.GetFieldInfo );
    //ok &= inHost.LoadFunction( inCoreDll, bridgeType, "GetFieldValue",       (void**)&outApi.GetFieldValue );
    //ok &= inHost.LoadFunction( inCoreDll, bridgeType, "SetFieldValue",       (void**)&outApi.SetFieldValue );
    //ok &= inHost.LoadFunction( inCoreDll, bridgeType, "GetInstanceCount",    (void**)&outApi.GetInstanceCount );
    //ok &= inHost.LoadFunction( inCoreDll, bridgeType, "GetInstanceHandle",   (void**)&outApi.GetInstanceHandle );
    //ok &= inHost.LoadFunction( inCoreDll, bridgeType, "GetInstanceTypeName", (void**)&outApi.GetInstanceTypeName );
    //ok &= inHost.LoadFunction( inCoreDll, bridgeType, "GetMethodCount",      (void**)&outApi.GetMethodCount );
    //ok &= inHost.LoadFunction( inCoreDll, bridgeType, "GetMethodName",       (void**)&outApi.GetMethodName );
    //ok &= inHost.LoadFunction( inCoreDll, bridgeType, "RestoreScript",       (void**)&outApi.RestoreScript );
    return ok;
}

int ScriptEngine::Init()
{
    if( gScriptHost.IsInitialized() )
    {
        DBG( "dotnet already up, skipping reinit" );
        return 0;
    }

    // TODO: make this less ugly, maybe derive from sharpmake or a config
    const std::string buildDir = ".build/" + std::string( build_prefix ) + std::string( build_platform ) + std::string( build_postfix );
    const std::string coreDll = buildDir + "/ScriptCore.dll";
    const std::string gameDll = buildDir + "/Game.Script.dll";
    const std::string runtimeConfig = "ScriptCore.runtimeconfig.json"; // lives in the root, not the build dir

    if( !gScriptHost.Init( runtimeConfig ) )
    {
        YIKES( "failed to init .NET runtime, scripting is dead" );
        return 1;
    }

    if( !LoadAPI( gScriptHost, coreDll, gDotnetAPI ) || !gDotnetAPI.IsValid() )
    {
        YIKES( "failed to load ScriptBridge API" );
        return 1;
    }

    // bind the engine side before any script can call into it
    ScriptEngineAPI engineApi{};
    engineApi.Log = Eng_Log;
    engineApi.GetTime = Eng_GetTime;
    Register_EntityBindings( engineApi );
    Register_TransformBindings( engineApi );
    Register_CameraBindings( engineApi );
    Register_BasicUIViewBindings( engineApi );
    Register_ImGuiBindings( engineApi );
    Register_InputBindings( engineApi );
    Register_WorldBindings( engineApi );
    if( gDotnetAPI.SetEngineAPI( &engineApi, sizeof( engineApi ) ) != 0 )
    {
        YIKES( "C# rejected our engine API, size mismatch? check EngineAPI vs EngineAPIBindings" );
        return 1;
    }

    if( gDotnetAPI.LoadGameAssembly( reinterpret_cast<const uint8_t*>( gameDll.c_str() ) ) != 0 )
    {
        YIKES_NEW( "failed to load game scripts from {}", gameDll );
        return 1;
    }

    int scriptCount = gDotnetAPI.GetScriptCount();
    DBG( "dotnet ready: {} script(s) available", scriptCount );

    return 0;
}

void ScriptEngine::SetWorld( WeakPtr<World> inWorld )
{
    ScriptBindings::SetWorld( inWorld );
}

int ScriptEngine::CreateScript( const std::string& inName, EntityID inEntity )
{
    return gDotnetAPI.CreateScript( reinterpret_cast<const uint8_t*>( inName.c_str() ), inEntity );
}

std::string ScriptEngine::GetFieldsJson( int inHandle )
{
    if( inHandle < 0 || !gDotnetAPI.GetFieldsJson )
    {
        return {};
    }

    // #TODO: fixed buffer, fine for small shit
    uint8_t buf[4096] = {};
    gDotnetAPI.GetFieldsJson( inHandle, buf, sizeof( buf ) );
    return reinterpret_cast<const char*>( buf );
}

void ScriptEngine::SetFieldsJson( int inHandle, const std::string& inJson )
{
    if( inHandle < 0 || inJson.empty() || !gDotnetAPI.SetFieldsJson )
    {
        return;
    }

    gDotnetAPI.SetFieldsJson( inHandle, reinterpret_cast<const uint8_t*>( inJson.c_str() ) );
}

void ScriptEngine::ScriptOnStart( int inHandle )
{
    gDotnetAPI.ScriptOnStart( inHandle );
}

void ScriptEngine::ScriptOnUpdate( int inHandle, float inDt )
{
    gDotnetAPI.ScriptOnUpdate( inHandle, inDt );
}

void ScriptEngine::ScriptOnDestroy( int inHandle )
{
    gDotnetAPI.ScriptOnDestroy( inHandle );
}

void ScriptEngine::ScriptOnEditorInspect( int inHandle )
{
    gDotnetAPI.ScriptOnEditorInspect( inHandle );
}

int ScriptEngine::GetScriptCount()
{
    return gDotnetAPI.GetScriptCount ? gDotnetAPI.GetScriptCount() : 0;
}

std::string ScriptEngine::GetScriptName( int inIndex )
{
    uint8_t buf[256] = {};
    if( gDotnetAPI.GetScriptName )
    {
        gDotnetAPI.GetScriptName( inIndex, buf, sizeof( buf ) );
    }

    return reinterpret_cast<const char*>( buf );
}

#endif