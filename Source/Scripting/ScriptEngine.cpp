#include "PCH.h"
#include "ScriptEngine.h"
#include "ScriptHost.h"

#if USING( ME_SCRIPTING )

#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <mutex>
#include <thread>
#include <SDL_filesystem.h>
#include "CLog.h"
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
#include "Bindings/Systems/Gameplay.bindings.h"
#include "Bindings/Systems/Navigation.bindings.h"

namespace fs = std::filesystem;


static ScriptHost gScriptHost;


static void Eng_Log( const uint8_t* inMsg )
{
    DBG( "{}", reinterpret_cast<const char*>( inMsg ) );
}
static float Eng_GetTime()
{
    // Seconds since the scripting runtime started (wall clock).
    static const auto start = std::chrono::steady_clock::now();
    return std::chrono::duration<float>( std::chrono::steady_clock::now() - start ).count();
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
using FnScriptOnFixedUpdate = void ( * )( int, float );
using FnScriptOnDestroy = void ( * )( int );
using FnGetMethodCount = int  ( * )( const uint8_t* );
using FnGetMethodName = void ( * )( const uint8_t*, int, uint8_t*, int );
using FnSetEngineAPI = int  ( * )( const ScriptEngineAPI*, int );
using FnRestoreScript = int  ( * )( const uint8_t*, int );
using FnOnEditorInspect = void  ( * )( int );
using FnGetFieldsJson = int ( * )( int, uint8_t*, int );
using FnIsHandleAlive = int ( * )( int );
using FnSetFieldsJson = void ( * )( int, const uint8_t* );
using FnScriptOnCollision = void ( * )( int, int, EntityID, const Vector3*, const Vector3* );

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
    FnScriptOnFixedUpdate ScriptOnFixedUpdate = nullptr;
    FnScriptOnDestroy     ScriptOnDestroy = nullptr;
    FnGetMethodCount      GetMethodCount = nullptr;
    FnGetMethodName       GetMethodName = nullptr;
    FnSetEngineAPI        SetEngineAPI = nullptr;
    FnRestoreScript       RestoreScript = nullptr;
    FnOnEditorInspect     ScriptOnEditorInspect = nullptr;
    FnGetFieldsJson       GetFieldsJson = nullptr;
    FnSetFieldsJson       SetFieldsJson = nullptr;
    FnIsHandleAlive       IsHandleAlive = nullptr;
    FnScriptOnCollision   ScriptOnCollision = nullptr;

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
    ok &= inHost.LoadFunction( inCoreDll, bridgeType, "ScriptOnFixedUpdate", (void**)&outApi.ScriptOnFixedUpdate );
    ok &= inHost.LoadFunction( inCoreDll, bridgeType, "ScriptOnDestroy", (void**)&outApi.ScriptOnDestroy );
    ok &= inHost.LoadFunction( inCoreDll, bridgeType, "ScriptOnEditorInspect", (void**)&outApi.ScriptOnEditorInspect );
    ok &= inHost.LoadFunction( inCoreDll, bridgeType, "GetFieldsJson", (void**)&outApi.GetFieldsJson );
    ok &= inHost.LoadFunction( inCoreDll, bridgeType, "SetFieldsJson", (void**)&outApi.SetFieldsJson );
    ok &= inHost.LoadFunction( inCoreDll, bridgeType, "ReloadGameAssembly", (void**)&outApi.ReloadGameAssembly );
    ok &= inHost.LoadFunction( inCoreDll, bridgeType, "IsHandleAlive", (void**)&outApi.IsHandleAlive );
    ok &= inHost.LoadFunction( inCoreDll, bridgeType, "ScriptOnCollision", (void**)&outApi.ScriptOnCollision );
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

namespace
{
    bool gAvailable = false;
    std::string gGameDll;

    // Hot reload: a rebuild runs on a worker thread; the swap happens on the main thread.
    std::thread gBuildThread;
    std::atomic<bool> gBuildRunning{ false };
    std::atomic<bool> gBuildDone{ false };
    bool gBuildSucceeded = false;
    bool gReloadQueued = false;
    std::mutex gBuildMutex;
    std::string gBuildLog;

    // Assemblies live next to the executable (the C# projects build into .build/<config>/).
    std::string ExecutableDirectory()
    {
        std::string directory;
        if( char* base = SDL_GetBasePath() )
        {
            directory = base;
            SDL_free( base );
        }
        return directory;
    }


    std::string FindRuntimeConfig( const std::string& InExeDir )
    {
        for( const std::string& candidate : { InExeDir + "ScriptCore.runtimeconfig.json", std::string( "ScriptCore.runtimeconfig.json" ), std::string( "Engine/Source/Scripting/ScriptCore.runtimeconfig.json" ) } )
        {
            std::error_code error;
            if( fs::exists( candidate, error ) )
            {
                return candidate;
            }
        }
        return "ScriptCore.runtimeconfig.json";
    }


    fs::file_time_type NewestSource( const std::vector<std::string>& InRoots )
    {
        fs::file_time_type newest = fs::file_time_type::min();
        for( const std::string& root : InRoots )
        {
            std::error_code error;
            if( fs::is_regular_file( root, error ) )
            {
                newest = std::max( newest, fs::last_write_time( root, error ) );
                continue;
            }
            for( fs::recursive_directory_iterator it( root, fs::directory_options::skip_permission_denied, error ), end; !error && it != end; it.increment( error ) )
            {
                const fs::path& path = it->path();
                if( path.extension() == ".cs" || path.extension() == ".csproj" )
                {
                    newest = std::max( newest, fs::last_write_time( path, error ) );
                }
            }
        }
        return newest;
    }


    const std::vector<std::string>& ScriptSources()
    {
        static const std::vector<std::string> sources = { "Assets", "Engine/Modules/ScriptCore/Source", ScriptEngine::kScriptProject, "Engine/Modules/ScriptCore/ScriptCore.csproj" };
        return sources;
    }
}


bool ScriptEngine::NeedsBuild()
{
    std::error_code error;
    if( !fs::exists( kScriptProject, error ) )
    {
        return false;   // no command-line project (Visual Studio builds the assemblies)
    }
    const fs::path gameDll = ExecutableDirectory() + "Game.Script.dll";
    if( !fs::exists( gameDll, error ) )
    {
        return true;
    }
    return NewestSource( ScriptSources() ) > fs::last_write_time( gameDll, error );
}


bool ScriptEngine::BuildScripts( std::string* OutLog )
{
#if USING( ME_TOOLS )
    std::error_code error;
    if( !fs::exists( kScriptProject, error ) )
    {
        return false;
    }
    std::string exeDir = ExecutableDirectory();
    if( !exeDir.empty() && ( exeDir.back() == '/' || exeDir.back() == '\\' ) )
    {
        exeDir.pop_back();
    }
#if USING( ME_DEBUG )
    const char* configuration = "Debug";
#else
    const char* configuration = "Release";
#endif
    // --artifacts-path keeps bin/obj out of the source tree.
    const std::string command = std::string( "dotnet build \"" ) + kScriptProject + "\" -c " + configuration + " -o \"" + exeDir + "\" --artifacts-path .tmp/dotnet --nologo -v q -clp:NoSummary 2>&1";
    std::string output;
#if USING( ME_PLATFORM_WINDOWS )
    FILE* pipe = _popen( command.c_str(), "r" );
#else
    FILE* pipe = popen( command.c_str(), "r" );
#endif
    if( !pipe )
    {
        if( OutLog )
        {
            *OutLog = "could not run dotnet";
        }
        return false;
    }
    char buffer[512];
    while( fgets( buffer, sizeof( buffer ), pipe ) )
    {
        output += buffer;
    }
#if USING( ME_PLATFORM_WINDOWS )
    const int status = _pclose( pipe );
#else
    const int status = pclose( pipe );
#endif
    if( OutLog )
    {
        *OutLog = output;
    }
    return status == 0;
#else
    return false;
#endif
}


int ScriptEngine::Init()
{
    if( gScriptHost.IsInitialized() )
    {
        DBG( "dotnet already up, skipping reinit" );
        return 0;
    }

    const std::string exeDir = ExecutableDirectory();
    const std::string coreDll = exeDir + "ScriptCore.dll";
    gGameDll = exeDir + "Game.Script.dll";

#if USING( ME_TOOLS )
    // Tools builds compile the scripts when they're missing or older than their sources.
    if( NeedsBuild() )
    {
        CLog::Log( CLog::LogType::Info, "Scripts: building " + std::string( kScriptProject ) );
        std::string log;
        if( !BuildScripts( &log ) )
        {
            YIKES( "Scripts: build failed\n" + log );
        }
    }
#endif

    std::error_code error;
    if( !fs::exists( coreDll, error ) || !fs::exists( gGameDll, error ) )
    {
        BRUH( "Scripts: no ScriptCore.dll / Game.Script.dll next to the executable; scripting is off" );
        return 1;
    }

    if( !gScriptHost.Init( FindRuntimeConfig( exeDir ) ) )
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
    Register_GameplayBindings( engineApi );
    Register_NavigationBindings( engineApi );

    // Every slot of the generated table must be filled, or a script call would jump to null.
    static_assert( sizeof( ScriptEngineAPI ) % sizeof( void* ) == 0, "ScriptEngineAPI holds only function pointers" );
    const void* const* slots = reinterpret_cast<const void* const*>( &engineApi );
    for( size_t slot = 0; slot < sizeof( ScriptEngineAPI ) / sizeof( void* ); ++slot )
    {
        if( !slots[slot] )
        {
            YIKES_NEW( "Script API slot {} has no binding (see Source/Scripting/ScriptAPI.def)", slot );
            return 1;
        }
    }
    if( gDotnetAPI.SetEngineAPI( &engineApi, sizeof( engineApi ) ) != 0 )
    {
        YIKES( "C# rejected our engine API, size mismatch? check EngineAPI vs EngineAPIBindings" );
        return 1;
    }

    if( gDotnetAPI.LoadGameAssembly( reinterpret_cast<const uint8_t*>( gGameDll.c_str() ) ) != 0 )
    {
        YIKES_NEW( "failed to load game scripts from {}", gGameDll );
        return 1;
    }

    gAvailable = true;
    int scriptCount = gDotnetAPI.GetScriptCount();
    DBG( "dotnet ready: {} script(s) available", scriptCount );

    return 0;
}


bool ScriptEngine::IsAvailable()
{
    return gAvailable;
}


void ScriptEngine::RequestReload()
{
#if USING( ME_TOOLS )
    if( !gAvailable )
    {
        return;
    }
    if( gBuildRunning )
    {
        gReloadQueued = true;   // build again once this one finishes
        return;
    }
    if( gBuildThread.joinable() )
    {
        gBuildThread.join();
    }
    CLog::Log( CLog::LogType::Info, "Scripts: changed, rebuilding" );
    gBuildRunning = true;
    gBuildDone = false;
    gBuildThread = std::thread( []() {
        std::string log;
        const bool succeeded = BuildScripts( &log );
        {
            std::lock_guard<std::mutex> lock( gBuildMutex );
            gBuildSucceeded = succeeded;
            gBuildLog = log;
        }
        gBuildDone = true;
        gBuildRunning = false;
    } );
#endif
}


bool ScriptEngine::PollReload()
{
#if USING( ME_TOOLS )
    if( !gBuildDone )
    {
        return false;
    }
    gBuildDone = false;
    if( gBuildThread.joinable() )
    {
        gBuildThread.join();
    }
    bool succeeded = false;
    std::string log;
    {
        std::lock_guard<std::mutex> lock( gBuildMutex );
        succeeded = gBuildSucceeded;
        log = gBuildLog;
    }
    bool reloaded = false;
    if( !succeeded )
    {
        YIKES( "Scripts: build failed; the running scripts are unchanged\n" + log );
    }
    else
    {
        reloaded = Reload();
    }
    if( gReloadQueued )
    {
        gReloadQueued = false;
        RequestReload();
    }
    return reloaded;
#else
    return false;
#endif
}


bool ScriptEngine::Reload()
{
    if( !gAvailable || !gDotnetAPI.ReloadGameAssembly )
    {
        return false;
    }
    const int restored = gDotnetAPI.ReloadGameAssembly( reinterpret_cast<const uint8_t*>( gGameDll.c_str() ) );
    if( restored < 0 )
    {
        YIKES( "Scripts: reload failed; the running scripts are unchanged" );
        return false;
    }
    CLog::Log( CLog::LogType::Info, "Scripts: hot reloaded (" + std::to_string( restored ) + " live script(s) kept their fields)" );
    return true;
}


bool ScriptEngine::IsHandleAlive( int inHandle )
{
    return inHandle >= 0 && gDotnetAPI.IsHandleAlive && gDotnetAPI.IsHandleAlive( inHandle ) != 0;
}


void ScriptEngine::Shutdown()
{
    if( gBuildThread.joinable() )
    {
        gBuildThread.join();
    }
    gAvailable = false;
    gScriptHost.Shutdown();
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

    // Try a small buffer first; the call reports the size it needs when that's not enough.
    std::string json( 1024, '\0' );
    int length = gDotnetAPI.GetFieldsJson( inHandle, reinterpret_cast<uint8_t*>( json.data() ), static_cast<int>( json.size() ) );
    if( length >= static_cast<int>( json.size() ) )
    {
        json.assign( static_cast<size_t>( length ) + 1, '\0' );
        length = gDotnetAPI.GetFieldsJson( inHandle, reinterpret_cast<uint8_t*>( json.data() ), static_cast<int>( json.size() ) );
    }
    json.resize( static_cast<size_t>( std::max( length, 0 ) ) );
    return json;
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

void ScriptEngine::ScriptOnFixedUpdate( int inHandle, float inDt )
{
    gDotnetAPI.ScriptOnFixedUpdate( inHandle, inDt );
}

void ScriptEngine::ScriptOnCollision( int inHandle, CollisionCallback inKind, EntityID inOther, const Vector3& inPoint, const Vector3& inNormal )
{
    if( gDotnetAPI.ScriptOnCollision )
    {
        gDotnetAPI.ScriptOnCollision( inHandle, static_cast<int>( inKind ), inOther, &inPoint, &inNormal );
    }
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