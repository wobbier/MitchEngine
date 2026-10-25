#include "EditorApp.h"
#include "Engine/Engine.h"
#include <Widgets/AssetBrowser.h>
#include "optick.h"
#include "Core/CommandLine.h"
#include "Resource/ResourceCache.h"
#include "Resource/MetaFile.h"
#include "CLog.h"
#include "Engine/ProjectSettings.h"
#include "Tools/Cook/Cooker.h"
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>
#include <vector>

// --cook-textures [folder]: cooks every texture under the folder (default Assets) whose .dds is
// missing or stale, on every core, without opening the editor. The editor would cook them one
// by one as scenes load them; Engine/Tools/SetTextureFormat.py deletes the .dds of textures it
// changes, so this recooks them in one go. Only textures with an engine .meta are cooked: one
// without (or with a Unity YAML .meta) is left for the editor to import when it first loads it.
// Returns non-zero if any cook failed.
static int CookTextures( const std::string& InFolder )
{
    // The engine isn't initialized here, so the log has no verbosity yet: progress and errors.
    CLog::GetInstance().SetLogVerbosity( CLog::LogType::Info );
    struct Job
    {
        Path Source;
        SharedPtr<MetaBase> Meta;
    };
    std::vector<Job> jobs;
    std::error_code error;
    for( std::filesystem::recursive_directory_iterator it( Path( InFolder ).FullPath, error ), end; it != end; it.increment( error ) )
    {
        const std::string path = it->path().generic_string();
        if( !it->is_regular_file( error ) || AssetBrowserWidget::GetAssetType( path ) != AssetType::Texture )
        {
            continue;
        }
        std::ifstream metaFile( path + ".meta" );
        char first = 0;
        if( !( metaFile >> first ) || first != '{' )
        {
            continue;
        }
        Path source( path, true );
        SharedPtr<MetaBase> meta = ResourceCache::GetInstance().LoadMetadata( source );
        if( meta && ( meta->FlaggedForExport || !std::filesystem::exists( path + ".dds", error ) ) )
        {
            jobs.push_back( { source, meta } );
        }
    }

    const auto start = std::chrono::steady_clock::now();
    std::atomic<size_t> next{ 0 };
    std::atomic<int> failed{ 0 };
    std::vector<std::thread> workers;
    for( unsigned i = 0; i < std::max( 1u, std::thread::hardware_concurrency() ); ++i )
    {
        workers.emplace_back( [&]() {
            for( size_t job = next++; job < jobs.size(); job = next++ )
            {
                ResourceCache::GetInstance().ExportIfNeeded( jobs[job].Source, jobs[job].Meta, true );
                std::error_code exists;
                if( !std::filesystem::exists( jobs[job].Source.FullPath + ".dds", exists ) )
                {
                    ++failed;
                }
            }
        } );
    }
    for( std::thread& worker : workers )
    {
        worker.join();
    }
    const double seconds = std::chrono::duration<double>( std::chrono::steady_clock::now() - start ).count();
    CLog::Log( failed ? CLog::LogType::Error : CLog::LogType::Info, "Cooked " + std::to_string( jobs.size() - failed ) + " of " + std::to_string( jobs.size() )
        + " texture(s) in " + std::to_string( static_cast<int>( seconds ) ) + " s" + ( failed ? " (" + std::to_string( failed.load() ) + " failed)" : "" ) );
    return failed ? 1 : 0;
}


// The game build next to this editor build (".build/Editor_Debug/Havana" ->
// ".build/Game_<platform>_Debug/Game_EntryPoint_x64"), for recording scenes; empty when absent.
static std::string FindGameExecutable( const char* InEditorPath )
{
    namespace fs = std::filesystem;
    std::error_code error;
    const fs::path editorDir = fs::absolute( InEditorPath, error ).parent_path();
    const std::string config = editorDir.filename().string().find( "Release" ) != std::string::npos ? "Release" : "Debug";
#if USING( ME_PLATFORM_WINDOWS )
    const char* platform = "win64";
    const char* executable = "Game_EntryPoint_x64.exe";
#elif USING( ME_PLATFORM_MACOS )
    const char* platform = "macos";
    const char* executable = "Game_EntryPoint_x64";
#else
    const char* platform = "linux";
    const char* executable = "Game_EntryPoint_x64";
#endif
    for( const std::string& candidate : { config, std::string( config == "Debug" ? "Release" : "Debug" ) } )
    {
        const fs::path game = editorDir.parent_path() / ( std::string( "Game_" ) + platform + "_" + candidate ) / executable;
        if( fs::is_regular_file( game, error ) )
        {
            return game.generic_string();
        }
    }
    return std::string();
}


// --cook [--scene <lvl>]... [--deps <recording>]... [--out <dir>] [--platform linux|windows|macos]
//        [--pak <file.mpak>] [--game <exe> | --no-record] [--record-frames <n>]
// The dependency cooker (SHIP-2, Engine/Docs/Cooking.md) without opening the editor: cooks what
// the scenes (default: the project's Build scenes) need into a tree that runs without sources.
// Scenes are recorded through the game first (--record-deps) when a game build is found.
// Returns non-zero if anything failed.
static int CookProject( const char* InEditorPath )
{
    // The engine isn't initialized here, so the log has no verbosity yet: progress and errors.
    CLog::GetInstance().SetLogVerbosity( CLog::LogType::Info );
    ProjectSettings::Get().Load();

    Cook::Request request;
    const std::vector<std::string>& args = CommandLine::GetArgs();
    for( size_t i = 0; i + 1 < args.size(); ++i )
    {
        if( args[i] == "--scene" ) request.Scenes.push_back( args[i + 1] );
        else if( args[i] == "--deps" ) request.DependencyFiles.push_back( args[i + 1] );
    }
    request.OutputDirectory = CommandLine::GetString( "--out", request.OutputDirectory );
    request.PakFile = CommandLine::GetString( "--pak" );
    request.RecordFrames = CommandLine::GetInt( "--record-frames", request.RecordFrames );
    const std::string platform = CommandLine::GetString( "--platform", Cook::PlatformName( Cook::HostPlatform() ) );
    if( !Cook::ParsePlatform( platform, request.TargetPlatform ) )
    {
        CLog::Log( CLog::LogType::Error, "Cook: unknown platform '" + platform + "' (linux, windows, macos)" );
        return 2;
    }
    if( !CommandLine::Has( "--no-record" ) )
    {
        request.GameExecutable = CommandLine::GetString( "--game", FindGameExecutable( InEditorPath ) );
        if( request.GameExecutable.empty() )
        {
            CLog::Log( CLog::LogType::Warning, "Cook: no game build found to record the scenes with (pass --game <exe>); assets only scripts load by name won't ship" );
        }
    }
    const Cook::Report report = Cook::Run( request );
    return report.Success ? 0 : 1;
}


int main( int argc, char** argv )
{
    CommandLine::Set( argc, argv );
    if( CommandLine::Has( "--cook-textures" ) )
    {
        return CookTextures( CommandLine::GetString( "--cook-textures", "Assets" ) );
    }
    if( CommandLine::Has( "--cook" ) )
    {
        return CookProject( argv[0] );
    }

    if( CommandLine::Has( "--capture-startup" ) )
    {
        OPTICK_START_CAPTURE();
    }
    EditorApp app( argc, argv );
    GetEngine().Init( &app );
    GetEngine().Run();
    return 0;
}
