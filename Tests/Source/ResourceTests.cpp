#include <doctest/doctest.h>
#include "Path.h"
#include "Resource/Resource.h"
#include "Resource/ResourceCache.h"
#include "Resource/AssetDatabase.h"
#include "Resource/MetaFile.h"
#include "Resource/MetaRegistry.h"
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

namespace ResourceTest
{
    std::atomic<int> s_delayMs{ 0 };

    // Reads its file in the background; "fail" in the name makes the background part fail.
    class SlowResource
        : public Resource
    {
    public:
        explicit SlowResource( const Path& InPath )
            : Resource( InPath )
        {
        }

        bool SupportsAsyncLoad() const override
        {
            return true;
        }

        bool Load() override
        {
            return LoadAsync() && FinishAsyncLoad();
        }

        bool LoadAsync() override
        {
            std::this_thread::sleep_for( std::chrono::milliseconds( s_delayMs.load() ) );
            BackgroundThread = std::this_thread::get_id();
            std::ifstream file( FilePath.FullPath );
            std::getline( file, Contents );
            return FilePath.FullPath.find( "fail" ) == std::string::npos;
        }

        bool FinishAsyncLoad() override
        {
            FinishThread = std::this_thread::get_id();
            Finished = true;
            return true;
        }

        std::thread::id BackgroundThread;
        std::thread::id FinishThread;
        std::string Contents;
        bool Finished = false;
    };

    class PlainResource
        : public Resource
    {
    public:
        explicit PlainResource( const Path& InPath )
            : Resource( InPath )
        {
        }

        bool Load() override
        {
            Loaded = true;
            return true;
        }

        bool Loaded = false;
    };

    Path WriteAsset( const std::string& InName, const std::string& InContents )
    {
        std::filesystem::create_directories( ".tmp/Tests/Resources" );
        const std::string path = ".tmp/Tests/Resources/" + InName + ".testasset";
        std::ofstream( path ) << InContents;
        return Path( path );
    }

    // Pumps like the engine does each frame until the resource has loaded (or 5 s pass).
    bool PumpUntilLoaded( const SharedPtr<Resource>& InResource )
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds( 5 );
        while( InResource->IsLoading() && std::chrono::steady_clock::now() < deadline )
        {
            ResourceCache::GetInstance().PumpAsyncLoads();
            std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
        }
        return !InResource->IsLoading();
    }
}

using namespace ResourceTest;

TEST_CASE( "Resources: async loads run on a loader thread and finish on the main thread" )
{
    ResourceCache& cache = ResourceCache::GetInstance();
    s_delayMs = 20;
    const Path path = WriteAsset( "AsyncBasic", "hello" );
    SharedPtr<SlowResource> resource = cache.GetAsync<SlowResource>( path );
    REQUIRE( resource );
    CHECK( resource->IsLoading() );
    CHECK_FALSE( resource->Finished );
    CHECK( cache.GetPendingLoadCount() >= 1 );
    CHECK( cache.GetAsync<SlowResource>( path ) == resource );   // one instance per path

    REQUIRE( PumpUntilLoaded( resource ) );
    CHECK( resource->Finished );
    CHECK_FALSE( resource->HasLoadFailed() );
    CHECK( resource->Contents == "hello" );
    CHECK( resource->BackgroundThread != std::this_thread::get_id() );
    CHECK( resource->FinishThread == std::this_thread::get_id() );
    CHECK( cache.GetPendingLoadCount() == 0 );
    s_delayMs = 0;
}

TEST_CASE( "Resources: Get finishes a resource that is still loading" )
{
    ResourceCache& cache = ResourceCache::GetInstance();
    s_delayMs = 30;
    const Path path = WriteAsset( "AsyncThenGet", "ready" );
    SharedPtr<SlowResource> pending = cache.GetAsync<SlowResource>( path );
    REQUIRE( pending );
    SharedPtr<SlowResource> loaded = cache.Get<SlowResource>( path );
    CHECK( loaded == pending );
    CHECK_FALSE( loaded->IsLoading() );
    CHECK( loaded->Finished );
    CHECK( loaded->Contents == "ready" );
    s_delayMs = 0;
}

TEST_CASE( "Resources: async failures, budgets, waits and cancellation" )
{
    ResourceCache& cache = ResourceCache::GetInstance();

    // A failing background part marks the resource failed without finishing it.
    SharedPtr<SlowResource> failing = cache.GetAsync<SlowResource>( WriteAsset( "AsyncfailCase", "x" ) );
    cache.WaitForAsyncLoads();
    CHECK_FALSE( failing->IsLoading() );
    CHECK( failing->HasLoadFailed() );
    CHECK_FALSE( failing->Finished );

    // Types without async support load on the spot.
    SharedPtr<PlainResource> plain = cache.GetAsync<PlainResource>( WriteAsset( "AsyncPlain", "x" ) );
    CHECK( plain->Loaded );
    CHECK_FALSE( plain->IsLoading() );

    // A zero budget finishes one load per pump.
    std::vector<SharedPtr<SlowResource>> batch;
    for( int i = 0; i < 6; ++i )
    {
        batch.push_back( cache.GetAsync<SlowResource>( WriteAsset( "AsyncBatch" + std::to_string( i ), std::to_string( i ) ) ) );
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds( 5 );
    int finished = 0;
    while( finished < 6 && std::chrono::steady_clock::now() < deadline )
    {
        const int now = cache.PumpAsyncLoads( 0.0 );
        CHECK( now <= 1 );
        finished += now;
    }
    CHECK( finished == 6 );
    for( int i = 0; i < 6; ++i )
    {
        CHECK( batch[i]->Contents == std::to_string( i ) );
    }

    // WaitForAsyncLoads finishes everything.
    s_delayMs = 10;
    std::vector<SharedPtr<SlowResource>> waited;
    for( int i = 0; i < 6; ++i )
    {
        waited.push_back( cache.GetAsync<SlowResource>( WriteAsset( "AsyncWait" + std::to_string( i ), "w" ) ) );
    }
    cache.WaitForAsyncLoads();
    CHECK( cache.GetPendingLoadCount() == 0 );
    for( const SharedPtr<SlowResource>& resource : waited )
    {
        CHECK( resource->Finished );
    }

    // Releasing the cache cancels what's still queued; nothing is left loading.
    s_delayMs = 50;
    std::vector<SharedPtr<SlowResource>> cancelled;
    for( int i = 0; i < 8; ++i )
    {
        cancelled.push_back( cache.GetAsync<SlowResource>( WriteAsset( "AsyncCancel" + std::to_string( i ), "c" ) ) );
    }
    cache.ReleaseAll();
    CHECK( cache.GetPendingLoadCount() == 0 );
    for( const SharedPtr<SlowResource>& resource : cancelled )
    {
        CHECK_FALSE( resource->IsLoading() );
    }
    s_delayMs = 0;

    // The loader threads come back for the next load.
    SharedPtr<SlowResource> after = cache.GetAsync<SlowResource>( WriteAsset( "AsyncAfterRelease", "again" ) );
    REQUIRE( PumpUntilLoaded( after ) );
    CHECK( after->Contents == "again" );
}


TEST_CASE( "Resources: loading a path whose asset moved this session loads it from the new path" )
{
    ResourceCache& cache = ResourceCache::GetInstance();
    const Path moved = WriteAsset( "MovedNew", "moved here" );
    const std::string oldPath = ".tmp/Tests/Resources/MovedOld.testasset";
    std::filesystem::remove( oldPath );
    AssetDatabase::Get().Register( oldPath, 0x5eed0000000000b1ull );
    AssetDatabase::Get().Move( oldPath, moved.GetLocalPathString() );

    SharedPtr<SlowResource> resource = cache.Get<SlowResource>( Path( oldPath ) );
    REQUIRE( resource );
    CHECK( resource->GetPath().FullPath == moved.FullPath );
    CHECK( resource->Contents == "moved here" );
    AssetDatabase::Get().Unregister( moved.GetLocalPathString() );
}


namespace ReimportTest
{
    std::atomic<int> s_exports{ 0 };
    std::atomic<int> s_exportDelayMs{ 0 };
    std::thread::id s_exportThread;

    // An asset whose cook is slow and runs off the main thread on hot reload (like a shader).
    struct SlowCookMeta
        : public MetaBase
    {
        explicit SlowCookMeta( const Path& InPath )
            : MetaBase( InPath )
        {
        }

        std::string GetExtension2() const override
        {
            return "slowcook";   // the source is its own "compiled" file
        }

        void OnSerialize( json& ) override
        {
        }

        void OnDeserialize( const json& ) override
        {
        }

        void Export() override
        {
            std::this_thread::sleep_for( std::chrono::milliseconds( s_exportDelayMs.load() ) );
            s_exportThread = std::this_thread::get_id();
            ++s_exports;
        }

        bool ExportsInBackground() const override
        {
            return true;
        }
    };

    class CookedResource
        : public Resource
    {
    public:
        explicit CookedResource( const Path& InPath )
            : Resource( InPath )
        {
        }

        void Reload() override
        {
            ++Reloads;
            ReloadThread = std::this_thread::get_id();
        }

        int Reloads = 0;
        std::thread::id ReloadThread;
    };
}

ME_REGISTER_METADATA( "slowcook", ReimportTest::SlowCookMeta );

TEST_CASE( "Resources: slow cooks recompile in the background on hot reload, then reload on the main thread" )
{
    using namespace ReimportTest;
    ResourceCache& cache = ResourceCache::GetInstance();
    std::filesystem::create_directories( ".tmp/Tests/Resources" );
    const Path path( ".tmp/Tests/Resources/Thing.slowcook" );
    std::ofstream( path.FullPath ) << "v1";
    SharedPtr<CookedResource> resource = cache.Get<CookedResource>( path );
    REQUIRE( resource );
    const int exportsBefore = s_exports.load();

    // The change is queued, not reloaded, and the main thread keeps going.
    s_exportDelayMs = 40;
    CHECK( cache.OnFilesChanged( { path.FullPath } ).empty() );
    CHECK( cache.GetPendingReimportCount() == 1 );
    CHECK( resource->Reloads == 0 );
    const std::vector<std::string> reloaded = cache.WaitForReimports();
    REQUIRE( reloaded.size() == 1 );
    CHECK( reloaded[0] == path.FullPath );
    CHECK( resource->Reloads == 1 );
    CHECK( s_exports.load() == exportsBefore + 1 );
    CHECK( s_exportThread != std::this_thread::get_id() );
    CHECK( resource->ReloadThread == std::this_thread::get_id() );

    // Changed again while compiling: compiled again, reloaded once with the latest.
    CHECK( cache.OnFilesChanged( { path.FullPath } ).empty() );
    std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );
    CHECK( cache.OnFilesChanged( { path.FullPath } ).empty() );
    cache.WaitForReimports();
    CHECK( s_exports.load() == exportsBefore + 3 );
    CHECK( resource->Reloads == 2 );
    CHECK( cache.GetPendingReimportCount() == 0 );
    s_exportDelayMs = 0;
}


namespace FirstCookTest
{
    std::atomic<int> s_exports{ 0 };
    std::thread::id s_exportThread;
    std::thread::id s_loadThread;

    // An asset with a separate compiled file ("<source>.cooked"), written by its cook.
    struct CookMeta
        : public MetaBase
    {
        explicit CookMeta( const Path& InPath )
            : MetaBase( InPath )
        {
        }

        std::string GetExtension2() const override
        {
            return "cooked";
        }

        void OnSerialize( json& ) override
        {
        }

        void OnDeserialize( const json& ) override
        {
        }

        void Export() override
        {
            s_exportThread = std::this_thread::get_id();
            ++s_exports;
            std::ofstream( FilePath.FullPath + ".cooked" ) << "cooked";
        }

        bool ExportsInBackground() const override
        {
            return true;
        }
    };

    class CookedAsset
        : public Resource
    {
    public:
        explicit CookedAsset( const Path& InPath )
            : Resource( InPath )
        {
        }

        bool SupportsAsyncLoad() const override
        {
            return true;
        }

        bool LoadAsync() override
        {
            s_loadThread = std::this_thread::get_id();
            std::ifstream file( FilePath.FullPath + ".cooked" );
            std::getline( file, Contents );
            return !Contents.empty();
        }

        bool FinishAsyncLoad() override
        {
            return true;
        }

        std::string Contents;
    };
}

ME_REGISTER_METADATA( "firstcook", FirstCookTest::CookMeta );

TEST_CASE( "Resources: a background load cooks a new asset on its loader thread" )
{
    using namespace FirstCookTest;
    ResourceCache& cache = ResourceCache::GetInstance();
    std::filesystem::create_directories( ".tmp/Tests/Resources" );
    const std::string source = ".tmp/Tests/Resources/Fresh.firstcook";
    std::filesystem::remove( source + ".cooked" );
    std::filesystem::remove( source + ".meta" );
    std::ofstream( source ) << "source";
    const int exportsBefore = s_exports.load();

    SharedPtr<CookedAsset> asset = cache.GetAsync<CookedAsset>( Path( source ) );
    REQUIRE( asset );
    // Pump (not WaitForAsyncLoads, which runs queued loads on this thread) until a loader is done.
    for( int i = 0; i < 5000 && asset->IsLoading(); ++i )
    {
        cache.PumpAsyncLoads();
        std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
    }
    CHECK_FALSE( asset->IsLoading() );
    CHECK_FALSE( asset->HasLoadFailed() );
    CHECK( asset->Contents == "cooked" );
    CHECK( s_exports.load() == exportsBefore + 1 );
    CHECK( s_exportThread != std::this_thread::get_id() );
    CHECK( s_exportThread == s_loadThread );
    CHECK( std::filesystem::exists( source + ".meta" ) );
}
