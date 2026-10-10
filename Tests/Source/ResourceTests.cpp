#include <doctest/doctest.h>
#include "Path.h"
#include "Resource/Resource.h"
#include "Resource/ResourceCache.h"
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
