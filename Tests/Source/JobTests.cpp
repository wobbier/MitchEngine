#include <doctest/doctest.h>

#include "Jobs/JobSystem.h"
#include "Profiling/FrameStats.h"
#include "JSON.h"
#include <atomic>
#include <chrono>
#include <cstring>
#include <numeric>
#include <thread>
#include <vector>

namespace
{
    void SpinFor( std::chrono::microseconds InDuration )
    {
        const auto end = std::chrono::steady_clock::now() + InDuration;
        while( std::chrono::steady_clock::now() < end )
        {
        }
    }

    uint64_t TotalBusy( const Jobs::Utilization& InUtilization, Jobs::LaneKind InKind )
    {
        uint64_t total = 0;
        for( const Jobs::LaneUtilization& lane : InUtilization.Lanes )
        {
            total += lane.Kind == InKind ? lane.BusyNanoseconds : 0;
        }
        return total;
    }

    uint32_t TotalJobs( const Jobs::Utilization& InUtilization )
    {
        uint32_t total = 0;
        for( const Jobs::LaneUtilization& lane : InUtilization.Lanes )
        {
            total += lane.Jobs;
        }
        return total;
    }
}

TEST_CASE( "Jobs: ParallelFor covers every index exactly once" )
{
    Jobs::JobSystem& jobs = Jobs::JobSystem::Get();
    REQUIRE( jobs.GetThreadCount() >= 2 );

    for( uint32_t count : { 1u, 7u, 64u, 1000u, 100000u } )
    {
        std::vector<std::atomic<int>> hits( count );
        jobs.ParallelFor( count, 16, [&hits]( uint32_t begin, uint32_t end ) {
            for( uint32_t i = begin; i < end; ++i )
            {
                hits[i].fetch_add( 1, std::memory_order_relaxed );
            }
        } );
        bool allOnce = true;
        for( auto& hit : hits )
        {
            allOnce &= hit.load() == 1;
        }
        CHECK( allOnce );
    }
}


TEST_CASE( "Jobs: Run + Wait completes every job, including nested waits" )
{
    Jobs::JobSystem& jobs = Jobs::JobSystem::Get();
    std::atomic<int> total{ 0 };
    Jobs::Counter outer;

    for( int i = 0; i < 64; ++i )
    {
        jobs.Run( outer, [&jobs, &total]() {
            // Nested parallelism from inside a job must not deadlock.
            Jobs::Counter inner;
            for( int j = 0; j < 8; ++j )
            {
                jobs.Run( inner, [&total]() { total.fetch_add( 1, std::memory_order_relaxed ); } );
            }
            jobs.Wait( inner );
        } );
    }
    jobs.Wait( outer );
    CHECK( total.load() == 64 * 8 );
    CHECK( outer.IsDone() );
}


TEST_CASE( "Jobs: repeated small ParallelFors (frame-like load)" )
{
    Jobs::JobSystem& jobs = Jobs::JobSystem::Get();
    std::vector<uint64_t> data( 4096 );
    std::iota( data.begin(), data.end(), 0 );

    uint64_t expected = 0;
    for( uint64_t value : data )
    {
        expected += value * 2;
    }

    for( int frame = 0; frame < 500; ++frame )
    {
        std::vector<uint64_t> doubled( data.size() );
        jobs.ParallelFor( static_cast<uint32_t>( data.size() ), 64, [&]( uint32_t begin, uint32_t end ) {
            for( uint32_t i = begin; i < end; ++i )
            {
                doubled[i] = data[i] * 2;
            }
        } );
        const uint64_t sum = std::accumulate( doubled.begin(), doubled.end(), uint64_t( 0 ) );
        if( sum != expected )
        {
            FAIL( "frame " << frame << " produced a wrong sum" );
        }
    }
}


TEST_CASE( "Jobs: utilization counts every batch and the workers' busy time (ME-320)" )
{
    Jobs::JobSystem& jobs = Jobs::JobSystem::Get();
    Jobs::Utilization utilization;
    jobs.TakeUtilization( utilization );   // start from zero

    REQUIRE( utilization.Lanes.size() == jobs.GetThreadCount() );
    CHECK( utilization.Lanes[0].Kind == Jobs::LaneKind::Main );
    CHECK( std::strcmp( utilization.Lanes[0].Name, "Main" ) == 0 );
    CHECK( utilization.Lanes[1].Kind == Jobs::LaneKind::Worker );
    CHECK( std::strcmp( utilization.Lanes[1].Name, "Job Worker 1" ) == 0 );

    // ~20 ms of work in batches: the workers take most of it.
    constexpr uint32_t kItems = 1000;
    constexpr auto kPerItem = std::chrono::microseconds( 20 );
    std::atomic<uint32_t> batches{ 0 };
    jobs.ParallelFor( "Utilization Test", kItems, 1, [&batches, kPerItem]( uint32_t begin, uint32_t end ) {
        batches.fetch_add( 1, std::memory_order_relaxed );
        SpinFor( kPerItem * ( end - begin ) );
    } );
    jobs.TakeUtilization( utilization );

    REQUIRE( batches.load() > 1 );
    // Every batch is one job, the one the caller ran itself included.
    CHECK( TotalJobs( utilization ) == batches.load() );
    CHECK( utilization.Lanes[0].Jobs >= 1 );
    uint32_t workerJobs = 0;
    for( size_t i = 1; i < utilization.Lanes.size(); ++i )
    {
        workerJobs += utilization.Lanes[i].Jobs;
    }
    CHECK( workerJobs > 0 );
    CHECK( TotalBusy( utilization, Jobs::LaneKind::Worker ) > 0 );
    // All lanes together were busy for at least the work done.
    const uint64_t work = static_cast<uint64_t>( std::chrono::nanoseconds( kPerItem * kItems ).count() );
    CHECK( TotalBusy( utilization, Jobs::LaneKind::Worker ) + TotalBusy( utilization, Jobs::LaneKind::Main ) >= work );

    // Taking resets the counters.
    jobs.TakeUtilization( utilization );
    CHECK( TotalJobs( utilization ) == 0 );
    CHECK( TotalBusy( utilization, Jobs::LaneKind::Worker ) == 0 );
    CHECK( utilization.WaitNanoseconds == 0 );
}


TEST_CASE( "Jobs: utilization counts nested jobs once each, and the main thread's wait (ME-320)" )
{
    Jobs::JobSystem& jobs = Jobs::JobSystem::Get();
    Jobs::Utilization utilization;
    jobs.TakeUtilization( utilization );

    Jobs::Counter outer;
    for( int i = 0; i < 16; ++i )
    {
        jobs.Run( outer, [&jobs]() {
            Jobs::Counter inner;
            for( int j = 0; j < 4; ++j )
            {
                jobs.Run( inner, []() { SpinFor( std::chrono::microseconds( 50 ) ); } );
            }
            jobs.Wait( inner );
        } );
    }
    jobs.Wait( outer );
    jobs.TakeUtilization( utilization );
    CHECK( TotalJobs( utilization ) == 16 + 16 * 4 );

    // A private system with one worker: a long job the main thread can only wait for.
    Jobs::JobSystem single;
    single.Init( 1 );
    Jobs::Utilization singleUtilization;
    single.TakeUtilization( singleUtilization );
    REQUIRE( singleUtilization.Lanes.size() == 2 );
    std::atomic<bool> started{ false };
    Jobs::Counter counter;
    single.Run( counter, [&started]() {
        started.store( true );
        std::this_thread::sleep_for( std::chrono::milliseconds( 20 ) );
    } );
    // Wait until the worker has it (the main thread would otherwise run it while waiting).
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds( 5 );
    while( !started.load() && std::chrono::steady_clock::now() < deadline )
    {
        std::this_thread::yield();
    }
    REQUIRE( started.load() );
    single.Wait( counter );
    single.TakeUtilization( singleUtilization );
    CHECK( singleUtilization.Lanes[1].Jobs == 1 );
    CHECK( singleUtilization.Lanes[1].BusyNanoseconds >= 15'000'000u );
    CHECK( singleUtilization.Lanes[0].Jobs == 0 );
    CHECK( singleUtilization.WaitNanoseconds >= 10'000'000u );
    single.Shutdown();
}


TEST_CASE( "FrameStats: EndFrame reports the job workers (ME-320)" )
{
    Jobs::JobSystem& jobs = Jobs::JobSystem::Get();
    FrameStats& stats = FrameStats::Get();
    stats.EndFrame( 16.0 );   // close whatever ran before

    jobs.ParallelFor( "FrameStats Test", 256, 1, []( uint32_t begin, uint32_t end ) { SpinFor( std::chrono::microseconds( 20 ) * ( end - begin ) ); } );
    stats.EndFrame( 16.0 );

    const FrameStats::JobStats& jobStats = stats.GetJobStats();
    CHECK( jobStats.WorkerCount == jobs.GetWorkerCount() );
    REQUIRE( jobStats.Lanes.size() == jobs.GetThreadCount() );
    CHECK( jobStats.Lanes[0].Name == "Main" );
    CHECK( jobStats.Jobs > 1 );
    CHECK( jobStats.WorkerBusyMilliseconds > 0.0 );
    CHECK( jobStats.MainJobs >= 1 );

    const json workers = FrameStats::ToJson( jobStats, 16.0 );
    CHECK( workers["Count"] == jobs.GetWorkerCount() );
    CHECK( workers["BusyMs"].size() == jobs.GetWorkerCount() );
    CHECK( workers["Jobs"] == jobStats.Jobs );
    CHECK( workers.contains( "WaitMs" ) );
    CHECK( workers["Utilization"].get<double>() > 0.0 );

    // Nothing ran since: the next frame reports no jobs.
    stats.EndFrame( 16.0 );
    CHECK( stats.GetJobStats().Jobs == 0 );
}
