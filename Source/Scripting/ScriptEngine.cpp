#include "PCH.h"
#include "ScriptEngine.h"
#include "ScriptHost.h"

#if USING( ME_SCRIPTING )

#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <regex>
#include <set>
#include <sstream>
#include <thread>
#include "CLog.h"
#include "Core/CommandLine.h"
#include "Engine/Project.h"
#include "VFS/VFS.h"
#include "Generated/ScriptEngineAPI.generated.h"
#include "Generated/ScriptBridgeAPI.generated.h"

// per-domain binding registrars
#include "Bindings/BindingContext.h"
#include "Bindings/Entity.bindings.h"
#include "Bindings/ImGui.bindings.h"
#include "Bindings/Components/Transform.bindings.h"
#include "Bindings/Components/Camera.bindings.h"
#include "Bindings/Systems/World.bindings.h"
#include "Bindings/Systems/Input.bindings.h"
#include "Bindings/Systems/Gameplay.bindings.h"
#include "Bindings/Systems/Navigation.bindings.h"
#include "Bindings/Systems/UIModel.bindings.h"
#include "Bindings/Systems/Settings.bindings.h"
#include "Bindings/Systems/SaveGame.bindings.h"
#include "Bindings/Systems/Localization.bindings.h"
#include "Bindings/Systems/Asset.bindings.h"
#include "Bindings/Systems/Query.bindings.h"
#include "Bindings/Systems/Net.bindings.h"

namespace fs = std::filesystem;


static ScriptHost gScriptHost;


static void Eng_Log( const uint8_t* inMsg ) noexcept
{
    DBG( "{}", reinterpret_cast<const char*>( inMsg ) );
}
static float Eng_GetTime() noexcept
{
    // Seconds since the scripting runtime started (wall clock).
    static const auto start = std::chrono::steady_clock::now();
    return std::chrono::duration<float>( std::chrono::steady_clock::now() - start ).count();
}


// The managed entry points: generated from ScriptBridge.def and filled in one call (ME-299).
static ScriptBridgeAPI gDotnetAPI;
static uint32_t gReloadGeneration = 0;

static bool LoadAPI( ScriptHost& inHost, const std::string& inCoreDll, ScriptBridgeAPI& outApi )
{
    int ( *getBridge )( ScriptBridgeAPI*, int, uint64_t ) = nullptr;
    if( !inHost.LoadFunction( inCoreDll, "ScriptCore.ScriptBridge, ScriptCore", "GetBridge", (void**)&getBridge ) || !getBridge )
    {
        YIKES( "ScriptCore.dll has no ScriptBridge.GetBridge (older than the engine?): rebuild ScriptCore" );
        return false;
    }
    const int result = getBridge( &outApi, static_cast<int>( sizeof( ScriptBridgeAPI ) ), kScriptBridgeAPIHash );
    if( result == -1 )
    {
        YIKES_NEW( "Script bridge mismatch: the engine was built against ScriptBridge.def {:016X} and ScriptCore.dll against another; rebuild ScriptCore (Engine/Tools/GenerateScriptAPI.py)", kScriptBridgeAPIHash );
    }
    else if( result != 0 )
    {
        YIKES( "ScriptCore.dll's ScriptBridgeAPI doesn't match the engine's (size): rebuild ScriptCore" );
    }
    return result == 0;
}

namespace
{
    bool gAvailable = false;
    std::string gGameDll;

    // Startup: StartInitAsync runs the runtime start on gInitThread; Init joins it. Either way the
    // start runs once and later calls return its result.
    std::thread gInitThread;
    std::mutex gInitMutex;
    int gInitResult = -1;   // -1 = not started

    // The last script build failed (Play waits until the scripts compile).
    std::atomic<bool> gHasBuildErrors{ false };
    // Its diagnostics (the startup build reports from the init thread, so behind a lock).
    std::mutex gDiagnosticsMutex;
    std::vector<ScriptEngine::BuildDiagnostic> gBuildDiagnostics;


    void SetBuildDiagnostics( std::vector<ScriptEngine::BuildDiagnostic> InDiagnostics )
    {
        std::lock_guard<std::mutex> lock( gDiagnosticsMutex );
        gBuildDiagnostics = std::move( InDiagnostics );
    }


    // Logs each diagnostic as its own entry, "Assets/File.cs:line:col: error CODE: text", so the Log
    // panel shows and opens every one. Returns the number of errors; the summary line is the caller's.
    int LogBuildDiagnostics( const std::string& InOutput )
    {
        int errors = 0;
        std::vector<ScriptEngine::BuildDiagnostic> diagnostics = ScriptEngine::ParseBuildDiagnostics( InOutput, fs::current_path().generic_string() );
        for( const ScriptEngine::BuildDiagnostic& diagnostic : diagnostics )
        {
            const std::string text = diagnostic.File + ":" + std::to_string( diagnostic.Line ) + ":" + std::to_string( diagnostic.Column ) + ": "
                + ( diagnostic.IsError ? "error " : "warning " ) + diagnostic.Code + ": " + diagnostic.Message;
            CLog::Log( diagnostic.IsError ? CLog::LogType::Error : CLog::LogType::Warning, text );
            errors += diagnostic.IsError ? 1 : 0;
        }
        SetBuildDiagnostics( std::move( diagnostics ) );
        return errors;
    }


    // A failed build's report: each diagnostic, then a summary (the raw output when nothing parsed).
    void ReportBuildFailure( const std::string& InOutput, const std::string& InWhat )
    {
        gHasBuildErrors = true;
        const int errors = LogBuildDiagnostics( InOutput );
        if( errors > 0 )
        {
            CLog::Log( CLog::LogType::Error, "Scripts: " + InWhat + " (" + std::to_string( errors ) + ( errors == 1 ? " error" : " errors" ) + ", listed above)" );
        }
        else
        {
            CLog::Log( CLog::LogType::Error, "Scripts: " + InWhat + "\n" + InOutput );
        }
    }

    // Hot reload: a rebuild runs on a worker thread; the swap happens on the main thread.
    std::thread gBuildThread;
    std::atomic<bool> gBuildRunning{ false };
    std::atomic<bool> gBuildDone{ false };
    bool gBuildSucceeded = false;
    bool gReloadQueued = false;
    std::mutex gBuildMutex;
    std::string gBuildLog;

    const char* BuildConfiguration()
    {
#if USING( ME_DEBUG )
        return "Debug";
#else
        return "Release";
#endif
    }


    // The running executable's name ("Havana", "Player").
    std::string ExecutableStem()
    {
        const std::vector<std::string>& args = CommandLine::GetArgs();
        const std::string stem = args.empty() ? std::string() : fs::path( args[0] ).stem().string();
        return stem.empty() ? std::string( "Game" ) : stem;
    }


    // A shared executable's script folder records the engine it built against, so a project opened
    // by another checkout's editor rebuilds instead of loading a ScriptCore it can't talk to.
    constexpr const char* kEngineStamp = "Engine.stamp";


    std::string FindRuntimeConfig( const std::string& InAssemblyDir )
    {
        for( const std::string& candidate : { InAssemblyDir + "ScriptCore.runtimeconfig.json", Project::GetExecutableDirectory() + "ScriptCore.runtimeconfig.json",
                                              std::string( "ScriptCore.runtimeconfig.json" ), Project::GetEngineRoot() + "/Source/Scripting/ScriptCore.runtimeconfig.json" } )
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
        static const std::vector<std::string> sources = { "Assets", Project::GetEngineRoot() + "/Modules/ScriptCore/Source", ScriptEngine::kScriptProject, Project::GetEngineRoot() + "/Modules/ScriptCore/ScriptCore.csproj" };
        return sources;
    }
}


std::string ScriptEngine::GetAssemblyDirectory()
{
#if USING( ME_TOOLS )
    // A shared editor or Player (another checkout's build, an install) builds each project's
    // scripts into the project, so projects never overwrite each other's assemblies.
    std::error_code error;
    if( fs::exists( kScriptProject, error ) && !Project::IsExecutableInProject() )
    {
        // A folder per executable and configuration, since the editor and the Player can run the
        // project at once (as a project's own builds do from their .build/<config>/ folders).
        return Project::GetRoot() + "/.tmp/Scripts/" + ExecutableStem() + "-" + BuildConfiguration() + "/";
    }
#endif
    return Project::GetExecutableDirectory();
}


bool ScriptEngine::NeedsBuild()
{
    std::error_code error;
    if( !fs::exists( kScriptProject, error ) )
    {
        return false;   // no command-line project (Visual Studio builds the assemblies)
    }
    const std::string assemblyDir = GetAssemblyDirectory();
    const fs::path gameDll = assemblyDir + "Game.Script.dll";
    if( !fs::exists( gameDll, error ) )
    {
        return true;
    }
    if( assemblyDir != Project::GetExecutableDirectory() )
    {
        std::string stamp;
        if( !VFS::Get().ReadText( Path( assemblyDir + kEngineStamp, true ), stamp ) || stamp != Project::GetEngineRoot() )
        {
            return true;
        }
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
    std::string outputDir = GetAssemblyDirectory();
    if( !outputDir.empty() && ( outputDir.back() == '/' || outputDir.back() == '\\' ) )
    {
        outputDir.pop_back();
    }
    // The script project finds ScriptCore through MitchEngineDir (default: the project's Engine/).
    // An engine outside the project is passed in, and written where IDEs pick it up too.
    const std::string engineDir = Project::GetEngineRoot() + "/";
    if( !Project::HasEmbeddedEngine() )
    {
        fs::create_directories( ".tmp", error );
        std::ofstream props( ".tmp/MitchEngine.props", std::ios::trunc );
        props << "<Project>\n  <!-- Written by the engine: the engine this project builds its scripts against. -->\n"
              << "  <PropertyGroup>\n    <MitchEngineDir>" << engineDir << "</MitchEngineDir>\n  </PropertyGroup>\n</Project>\n";
    }
    // --artifacts-path keeps bin/obj out of the source tree.
    const std::string command = std::string( "dotnet build \"" ) + kScriptProject + "\" -c " + BuildConfiguration() + " -o \"" + outputDir + "\" -p:MitchEngineDir=\"" + engineDir
        + "\" --artifacts-path .tmp/dotnet --nologo -v q -clp:NoSummary 2>&1";
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
    if( status == 0 && outputDir + "/" != Project::GetExecutableDirectory() )
    {
        std::ofstream( outputDir + "/" + kEngineStamp, std::ios::trunc ) << Project::GetEngineRoot();
    }
    return status == 0;
#else
    return false;
#endif
}


// Starts .NET and loads the script assemblies (Init's body). Safe on a worker thread: nothing in
// it touches engine state, and the managed side keeps no thread affinity.
static int StartRuntime()
{
    if( gScriptHost.IsInitialized() )
    {
        DBG( "dotnet already up, skipping reinit" );
        return 0;
    }

    const std::string exeDir = Project::GetExecutableDirectory();
    const std::string assemblyDir = ScriptEngine::GetAssemblyDirectory();
    const std::string coreDll = assemblyDir + "ScriptCore.dll";
    gGameDll = assemblyDir + "Game.Script.dll";

#if USING( ME_TOOLS )
    // Tools builds compile the scripts when they're missing or older than their sources.
    if( ScriptEngine::NeedsBuild() )
    {
        CLog::Log( CLog::LogType::Info, "Scripts: building " + std::string( ScriptEngine::kScriptProject ) );
        std::string log;
        if( !ScriptEngine::BuildScripts( &log ) )
        {
            ReportBuildFailure( log, "build failed" );
        }
    }
#endif

    std::error_code error;
    if( !fs::exists( coreDll, error ) || !fs::exists( gGameDll, error ) )
    {
        BRUH( "Scripts: no ScriptCore.dll / Game.Script.dll in " + assemblyDir + "; scripting is off" );
        return 1;
    }

    // Build Game ships a private .NET install next to the executable (Docs/Building.md), so
    // players need none; without one the system's is used.
    std::string dotnetRoot = exeDir + "dotnet";
    if( !fs::is_directory( dotnetRoot + "/host/fxr", error ) )
    {
        dotnetRoot.clear();
    }
    if( !gScriptHost.Init( FindRuntimeConfig( assemblyDir ), dotnetRoot ) )
    {
        YIKES( "failed to init .NET runtime, scripting is dead" );
        return 1;
    }

    if( !LoadAPI( gScriptHost, coreDll, gDotnetAPI ) )
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
    Register_ImGuiBindings( engineApi );
    Register_InputBindings( engineApi );
    Register_WorldBindings( engineApi );
    Register_GameplayBindings( engineApi );
    Register_NavigationBindings( engineApi );
    Register_UIModelBindings( engineApi );
    Register_SettingsBindings( engineApi );
    Register_SaveGameBindings( engineApi );
    Register_LocalizationBindings( engineApi );
    Register_AssetBindings( engineApi );
    Register_QueryBindings( engineApi );
    Register_NetBindings( engineApi );

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
    // Same manifest on both sides? A stale ScriptCore.dll beside a newer executable would otherwise
    // call slots with the wrong signatures (ME-298).
    uint64_t managedHash = 0;
    gDotnetAPI.GetEngineAPIHash( &managedHash );
    if( managedHash != kScriptEngineAPIHash )
    {
        YIKES_NEW( "Script API mismatch: the engine was built against {:016X}, ScriptCore.dll against {:016X}; rebuild ScriptCore (Engine/Tools/GenerateScriptAPI.py)", kScriptEngineAPIHash, managedHash );
        return 1;
    }
    if( gDotnetAPI.SetEngineAPI( &engineApi, sizeof( engineApi ) ) != 0 )
    {
        YIKES( "C# rejected our engine API (size or struct layout mismatch: see the [ScriptBridge] line above)" );
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


void ScriptEngine::StartInitAsync()
{
    std::lock_guard<std::mutex> lock( gInitMutex );
    if( gInitResult >= 0 || gInitThread.joinable() )
    {
        return;
    }
    gInitThread = std::thread( []() {
        const int result = StartRuntime();
        std::lock_guard<std::mutex> lock( gInitMutex );
        gInitResult = result;
    } );
}


int ScriptEngine::Init()
{
    if( gInitThread.joinable() )
    {
        gInitThread.join();
    }
    std::lock_guard<std::mutex> lock( gInitMutex );
    if( gInitResult < 0 )
    {
        gInitResult = StartRuntime();
    }
    return gInitResult;
}


std::vector<ScriptEngine::BuildDiagnostic> ScriptEngine::ParseBuildDiagnostics( const std::string& InOutput, const std::string& InRoot )
{
    // "<path>(line,col): error|warning CODE: text [project]", once per diagnostic (MSBuild can repeat them).
    static const std::regex pattern( R"(^\s*(.+?\.cs)\((\d+),(\d+)\): (error|warning) (\w+): (.*?)( \[[^\]]*\])?\s*$)" );
    std::string root = InRoot;
    std::replace( root.begin(), root.end(), '\\', '/' );
    if( !root.empty() && root.back() != '/' )
    {
        root += '/';
    }
    std::vector<BuildDiagnostic> diagnostics;
    std::set<std::string> seen;
    std::istringstream lines( InOutput );
    std::string line;
    while( std::getline( lines, line ) )
    {
        std::smatch match;
        if( !std::regex_match( line, match, pattern ) || !seen.insert( match.str( 1 ) + match.str( 2 ) + match.str( 3 ) + match.str( 5 ) + match.str( 6 ) ).second )
        {
            continue;
        }
        BuildDiagnostic diagnostic;
        diagnostic.File = match.str( 1 );
        std::replace( diagnostic.File.begin(), diagnostic.File.end(), '\\', '/' );
        if( !root.empty() && diagnostic.File.rfind( root, 0 ) == 0 )
        {
            diagnostic.File = diagnostic.File.substr( root.size() );
        }
        diagnostic.Line = std::stoi( match.str( 2 ) );
        diagnostic.Column = std::stoi( match.str( 3 ) );
        diagnostic.IsError = match.str( 4 ) == "error";
        diagnostic.Code = match.str( 5 );
        diagnostic.Message = match.str( 6 );
        diagnostics.push_back( std::move( diagnostic ) );
    }
    return diagnostics;
}


bool ScriptEngine::HasBuildErrors()
{
    return gHasBuildErrors;
}


std::vector<ScriptEngine::BuildDiagnostic> ScriptEngine::GetBuildDiagnostics()
{
    std::lock_guard<std::mutex> lock( gDiagnosticsMutex );
    return gBuildDiagnostics;
}


size_t ScriptEngine::GetBuildErrorCount()
{
    std::lock_guard<std::mutex> lock( gDiagnosticsMutex );
    return static_cast<size_t>( std::count_if( gBuildDiagnostics.begin(), gBuildDiagnostics.end(), []( const BuildDiagnostic& InDiagnostic ) { return InDiagnostic.IsError; } ) );
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
        ReportBuildFailure( log, "build failed; the running scripts are unchanged" );
    }
    else
    {
        gHasBuildErrors = false;
        SetBuildDiagnostics( {} );
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
    ++gReloadGeneration;
    CLog::Log( CLog::LogType::Info, "Scripts: hot reloaded (" + std::to_string( restored ) + " live script(s) kept their fields)" );
    return true;
}


bool ScriptEngine::IsHandleAlive( int inHandle )
{
    return inHandle >= 0 && gDotnetAPI.IsHandleAlive && gDotnetAPI.IsHandleAlive( inHandle ) != 0;
}


void ScriptEngine::Shutdown()
{
    if( gInitThread.joinable() )
    {
        gInitThread.join();
    }
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

std::string ScriptEngine::GetFieldsJson( int inHandle, bool InIncludeSaveFields )
{
    const auto getFields = InIncludeSaveFields ? gDotnetAPI.GetSaveFieldsJson : gDotnetAPI.GetFieldsJson;
    if( inHandle < 0 || !getFields )
    {
        return {};
    }

    // Try a small buffer first; the call reports the size it needs when that's not enough.
    std::string json( 1024, '\0' );
    int length = getFields( inHandle, reinterpret_cast<uint8_t*>( json.data() ), static_cast<int>( json.size() ) );
    if( length >= static_cast<int>( json.size() ) )
    {
        json.assign( static_cast<size_t>( length ) + 1, '\0' );
        length = getFields( inHandle, reinterpret_cast<uint8_t*>( json.data() ), static_cast<int>( json.size() ) );
    }
    json.resize( static_cast<size_t>( std::max( length, 0 ) ) );
    return json;
}

std::vector<std::string> ScriptEngine::GetDataAssetTypes()
{
    std::vector<std::string> types;
    if( !gDotnetAPI.GetDataAssetTypes )
    {
        return types;
    }
    std::string names( 1024, '\0' );
    int length = gDotnetAPI.GetDataAssetTypes( reinterpret_cast<uint8_t*>( names.data() ), static_cast<int>( names.size() ) );
    if( length >= static_cast<int>( names.size() ) )
    {
        names.assign( static_cast<size_t>( length ) + 1, '\0' );
        length = gDotnetAPI.GetDataAssetTypes( reinterpret_cast<uint8_t*>( names.data() ), static_cast<int>( names.size() ) );
    }
    names.resize( static_cast<size_t>( std::max( length, 0 ) ) );
    size_t start = 0;
    while( start < names.size() )
    {
        const size_t end = names.find( '\n', start );
        types.push_back( names.substr( start, end == std::string::npos ? std::string::npos : end - start ) );
        if( end == std::string::npos )
        {
            break;
        }
        start = end + 1;
    }
    return types;
}


void ScriptEngine::InspectDataAsset( const std::string& inPath )
{
    if( gDotnetAPI.InspectDataAsset )
    {
        gDotnetAPI.InspectDataAsset( reinterpret_cast<const uint8_t*>( inPath.c_str() ) );
    }
}


void ScriptEngine::SchedulerTick( float inDeltaTime, bool inReset )
{
    if( gDotnetAPI.SchedulerTick )
    {
        gDotnetAPI.SchedulerTick( inDeltaTime, inReset ? 1 : 0 );
    }
}


void ScriptEngine::DispatchEngineEvents( const std::string& inJsonArray )
{
    if( gDotnetAPI.DispatchEngineEvents && !inJsonArray.empty() )
    {
        gDotnetAPI.DispatchEngineEvents( 0, reinterpret_cast<const uint8_t*>( inJsonArray.c_str() ) );
    }
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


void ScriptEngine::ScriptOnLateUpdate( int inHandle, float inDt )
{
    gDotnetAPI.ScriptOnLateUpdate( inHandle, inDt );
}


uint32_t ScriptEngine::NetGetFlags( int inHandle )
{
    return inHandle >= 0 && gDotnetAPI.NetGetFlags ? static_cast<uint32_t>( std::max( 0, gDotnetAPI.NetGetFlags( inHandle ) ) ) : 0;
}


namespace
{
    // The script fills a fixed buffer (net state is small and fixed-size); it reports the size it
    // needs, so a bigger one is a retry.
    size_t CallNetWriter( int ( *InWrite )( int, uint8_t*, int ), int inHandle, std::vector<uint8_t>& outBytes )
    {
        if( inHandle < 0 || !InWrite )
        {
            return 0;
        }
        uint8_t buffer[1024];
        int needed = InWrite( inHandle, buffer, static_cast<int>( sizeof( buffer ) ) );
        if( needed <= 0 )
        {
            return 0;
        }
        if( needed <= static_cast<int>( sizeof( buffer ) ) )
        {
            outBytes.insert( outBytes.end(), buffer, buffer + needed );
            return static_cast<size_t>( needed );
        }
        std::vector<uint8_t> bigger( static_cast<size_t>( needed ) );
        needed = InWrite( inHandle, bigger.data(), static_cast<int>( bigger.size() ) );
        if( needed <= 0 || needed > static_cast<int>( bigger.size() ) )
        {
            return 0;
        }
        outBytes.insert( outBytes.end(), bigger.begin(), bigger.begin() + needed );
        return static_cast<size_t>( needed );
    }
}


size_t ScriptEngine::NetSavePredicted( int inHandle, std::vector<uint8_t>& outBytes )
{
    return CallNetWriter( gDotnetAPI.NetSavePredicted, inHandle, outBytes );
}


void ScriptEngine::NetRestorePredicted( int inHandle, const std::vector<uint8_t>& inBytes )
{
    if( inHandle >= 0 && gDotnetAPI.NetRestorePredicted && !inBytes.empty() )
    {
        gDotnetAPI.NetRestorePredicted( inHandle, inBytes.data(), static_cast<int>( inBytes.size() ) );
    }
}


size_t ScriptEngine::NetWriteReplicated( int inHandle, std::vector<uint8_t>& outBytes )
{
    return CallNetWriter( gDotnetAPI.NetWriteReplicated, inHandle, outBytes );
}


void ScriptEngine::NetReadReplicated( int inHandle, const std::vector<uint8_t>& inBytes, bool inSkipPredicted )
{
    if( inHandle >= 0 && gDotnetAPI.NetReadReplicated && !inBytes.empty() )
    {
        gDotnetAPI.NetReadReplicated( inHandle, inBytes.data(), static_cast<int>( inBytes.size() ), inSkipPredicted ? 1 : 0 );
    }
}


void ScriptEngine::NetDispatchRpc( int inHandle, int inRpcId, const uint8_t* inData, size_t inSize )
{
    if( inHandle >= 0 && gDotnetAPI.NetDispatchRpc )
    {
        gDotnetAPI.NetDispatchRpc( inHandle, inRpcId, inData, static_cast<int>( inSize ) );
    }
}


uint64_t ScriptEngine::NetSchemaHash()
{
    uint64_t hash = 0;
    if( gDotnetAPI.NetSchemaHash )
    {
        gDotnetAPI.NetSchemaHash( &hash );
    }
    return hash;
}


ScriptEngine::Schedule ScriptEngine::GetSchedule( int inHandle )
{
    Schedule schedule;
    if( inHandle < 0 || !gDotnetAPI.ScriptGetSchedule )
    {
        return schedule;
    }
    int order = 0;
    const int callbacks = gDotnetAPI.ScriptGetSchedule( inHandle, &order );
    if( callbacks >= 0 )
    {
        schedule.Order = order;
        schedule.Callbacks = static_cast<uint32_t>( callbacks );
    }
    return schedule;
}


uint32_t ScriptEngine::GetReloadGeneration()
{
    return gReloadGeneration;
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


std::string ScriptEngine::GetHandleTypeName( int inHandle )
{
    uint8_t buf[256] = {};
    if( gDotnetAPI.GetHandleTypeName )
    {
        gDotnetAPI.GetHandleTypeName( inHandle, buf, sizeof( buf ) );
    }
    return reinterpret_cast<const char*>( buf );
}


bool ScriptEngine::TakeScriptFault( EntityID& outEntity )
{
    outEntity = EntityID();
    return gDotnetAPI.TakeScriptFault && gDotnetAPI.TakeScriptFault( &outEntity ) != 0;
}

#endif