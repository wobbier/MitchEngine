#include "EditorApp.h"
#include "Engine/Engine.h"
#include <Widgets/AssetBrowser.h>
#include "optick.h"
#include "Core/CommandLine.h"
#include "Resource/ResourceCache.h"
#include "Resource/MetaFile.h"
#include "CLog.h"
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


int main( int argc, char** argv )
{
    CommandLine::Set( argc, argv );
    if( CommandLine::Has( "--cook-textures" ) )
    {
        return CookTextures( CommandLine::GetString( "--cook-textures", "Assets" ) );
    }
    for( int i = 0; i < argc; ++i )
    {
        std::printf( "argv[%d]: %s\n", i, argv[i] );
        if( std::string( argv[i] ) == "-CompileAssets" )
        {
            bgfx::Init init;
            init.reset = BGFX_RESET_VSYNC;

            bgfx::init( init );
            AssetBrowserWidget browser = AssetBrowserWidget( nullptr );
            browser.BuildAssets();
            return 0;
        }
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
