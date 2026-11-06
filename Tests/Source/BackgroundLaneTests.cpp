#include <doctest/doctest.h>

#include "Jobs/BackgroundLane.h"
#include "Profiling/FrameStats.h"
#include "JSON.h"
#include <atomic>
#include <chrono>
#include <future>
#include <stdexcept>
#include <thread>
#include <vector>

// ME-319: the background lane (Jobs::BackgroundLane) and its serial queues.

namespace
{
    // Polls InDone for up to InTimeout (a hang fails the test instead of the run).
    template<typename Fn>
    bool WaitUntil( Fn&& InDone, std::chrono::milliseconds InTimeout = std::chrono::seconds( 10 ) )
    {
        const auto end = std::chrono::steady_clock::now() + InTimeout;
        while( !InDone() )
        {
            if( std::chrono::steady_clock::now() > end )
            {
                return false;
            }
            std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
        }
        return true;
    }

    void SpinFor( std::chrono::microseconds InDuration )
    {
        const auto end = std::chrono::steady_clock::now() + InDuration;
        while( std::chrono::steady_clock::now() < end )
        {
        }
    }
}


TEST_CASE( "BackgroundLane: Submit runs on a named lane thread and returns the result or exception (ME-319)" )
{
    Jobs::BackgroundLane& lane = Jobs::BackgroundLane::Get();
    REQUIRE( lane.IsRunning() );
    CHECK( lane.GetThreadCount() >= 2 );
    CHECK( lane.GetThreadCount() <= 4 );
    CHECK( Jobs::BackgroundLane::GetCurrentThreadIndex() == 0 );

    std::future<uint32_t> index = lane.Submit( "Test Index", []() { return Jobs::BackgroundLane::GetCurrentThreadIndex(); } );
    REQUIRE( index.wait_for( std::chrono::seconds( 10 ) ) == std::future_status::ready );
    const uint32_t ran = index.get();
    CHECK( ran >= 1 );
    CHECK( ran <= lane.GetThreadCount() );

    std::future<int> failing = lane.Submit( "Test Throw", []() -> int { throw std::runtime_error( "boom" ); } );
    REQUIRE( failing.wait_for( std::chrono::seconds( 10 ) ) == std::future_status::ready );
    CHECK_THROWS_AS( failing.get(), std::runtime_error );

    // Move-only captures work (a replay buffer handed over by value).
    std::vector<uint8_t> buffer( 1024, 7 );
    auto owned = std::make_unique<std::vector<uint8_t>>( std::move( buffer ) );
    std::future<size_t> size = lane.Submit( "Test Move", [data = std::move( owned )]() { return data->size(); } );
    CHECK( size.get() == 1024 );
}


TEST_CASE( "BackgroundLane: Low tasks never take the last thread, so Normal work always runs (ME-319)" )
{
    Jobs::BackgroundLane lane;
    lane.Init( 2 );
    std::atomic<bool> release{ false };
    std::atomic<int> lowStarted{ 0 };
    std::vector<std::future<void>> lows;
    for( int i = 0; i < 3; ++i )
    {
        lows.push_back( lane.Submit( "Test Low", [&]() {
            lowStarted.fetch_add( 1 );
            while( !release.load() ) std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
        }, Jobs::BackgroundPriority::Low ) );
    }
    REQUIRE( WaitUntil( [&]() { return lowStarted.load() >= 1; } ) );
    std::this_thread::sleep_for( std::chrono::milliseconds( 20 ) );
    // Two threads: one Low at a time; the other two wait.
    CHECK( lowStarted.load() == 1 );

    std::future<int> normal = lane.Submit( "Test Normal", []() { return 42; } );
    REQUIRE( normal.wait_for( std::chrono::seconds( 10 ) ) == std::future_status::ready );
    CHECK( normal.get() == 42 );

    release.store( true );
    for( std::future<void>& low : lows )
    {
        REQUIRE( low.wait_for( std::chrono::seconds( 10 ) ) == std::future_status::ready );
    }
    CHECK( lowStarted.load() == 3 );
    lane.Shutdown();
}


TEST_CASE( "BackgroundLane: ParallelFor covers every index once, even from a lane task with every thread busy (ME-319)" )
{
    Jobs::BackgroundLane lane;
    lane.Init( 2 );

    for( uint32_t count : { 0u, 1u, 7u, 1000u } )
    {
        std::vector<std::atomic<int>> hits( count );
        lane.ParallelFor( "Test ParallelFor", count, [&hits]( uint32_t i ) { hits[i].fetch_add( 1 ); } );
        bool allOnce = true;
        for( auto& hit : hits ) allOnce = allOnce && hit.load() == 1;
        CHECK( allOnce );
    }

    // Both threads busy: one blocked, the other running a ParallelFor whose helpers can't start.
    // The caller has to do all of it itself rather than wait on the lane.
    std::atomic<bool> release{ false };
    std::future<void> blocker = lane.Submit( "Test Blocker", [&]() {
        while( !release.load() ) std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
    } );
    std::future<int> nested = lane.Submit( "Test Nested", [&lane]() {
        std::vector<std::atomic<int>> hits( 500 );
        lane.ParallelFor( "Test Nested Batch", 500, [&hits]( uint32_t i ) { hits[i].fetch_add( 1 ); } );
        int total = 0;
        for( auto& hit : hits ) total += hit.load();
        return total;
    } );
    REQUIRE( nested.wait_for( std::chrono::seconds( 10 ) ) == std::future_status::ready );
    CHECK( nested.get() == 500 );
    release.store( true );
    blocker.wait();

    // Exceptions reach the caller once every index has been tried.
    std::atomic<int> tried{ 0 };
    CHECK_THROWS_AS( lane.ParallelFor( "Test Throw", 64, [&tried]( uint32_t i ) {
        tried.fetch_add( 1 );
        if( i == 10 ) throw std::runtime_error( "index 10" );
    } ), std::runtime_error );
    CHECK( tried.load() == 64 );
    lane.Shutdown();
}


TEST_CASE( "BackgroundLane: Shutdown runs what is queued; afterwards tasks run inline (ME-319)" )
{
    Jobs::BackgroundLane lane;
    lane.Init( 1 );
    std::atomic<bool> release{ false };
    std::atomic<int> ran{ 0 };
    lane.Post( "Test Blocker", [&]() {
        while( !release.load() ) std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
    } );
    for( int i = 0; i < 10; ++i )
    {
        lane.Post( "Test Queued", [&ran]() { ran.fetch_add( 1 ); } );
    }
    CHECK( lane.GetQueuedCount() >= 9 );
    std::thread releaser( [&]() {
        std::this_thread::sleep_for( std::chrono::milliseconds( 20 ) );
        release.store( true );
    } );
    lane.Shutdown();
    releaser.join();
    CHECK( ran.load() == 10 );
    CHECK_FALSE( lane.IsRunning() );

    const std::thread::id caller = std::this_thread::get_id();
    std::future<bool> inlineRun = lane.Submit( "Test Inline", [caller]() { return std::this_thread::get_id() == caller; } );
    CHECK( inlineRun.get() );
}


TEST_CASE( "BackgroundLane: utilization counts task time per thread, a running task up to now (ME-319)" )
{
    Jobs::BackgroundLane lane;
    lane.Init( 2 );
    Jobs::Utilization before;
    lane.AppendUtilization( before );   // start from zero
    REQUIRE( before.Lanes.size() == 2 );
    CHECK( before.Lanes[0].Kind == Jobs::LaneKind::Background );
    CHECK( std::string( before.Lanes[0].Name ) == "Background 1" );

    std::atomic<bool> release{ false };
    std::atomic<bool> started{ false };
    std::future<void> running = lane.Submit( "Test Long", [&]() {
        started.store( true );
        while( !release.load() ) SpinFor( std::chrono::microseconds( 100 ) );
    } );
    REQUIRE( WaitUntil( [&]() { return started.load(); } ) );
    std::this_thread::sleep_for( std::chrono::milliseconds( 20 ) );

    Jobs::Utilization during;
    lane.AppendUtilization( during );
    uint64_t busy = 0;
    uint32_t jobs = 0;
    for( const Jobs::LaneUtilization& entry : during.Lanes )
    {
        busy += entry.BusyNanoseconds;
        jobs += entry.Jobs;
    }
    // Still running: its time so far counts this frame; it isn't a finished job yet.
    CHECK( busy >= 15'000'000u );
    CHECK( jobs == 0 );

    release.store( true );
    running.wait();
    // The future is ready a moment before the task's thread books it: read until it has.
    busy = 0;
    jobs = 0;
    CHECK( WaitUntil( [&]() {
        Jobs::Utilization after;
        lane.AppendUtilization( after );
        for( const Jobs::LaneUtilization& entry : after.Lanes )
        {
            busy += entry.BusyNanoseconds;
            jobs += entry.Jobs;
        }
        return jobs > 0;
    } ) );
    CHECK( jobs == 1 );
    // Only the part since the last read: not the 20 ms again.
    CHECK( busy < 15'000'000u );
    lane.Shutdown();
}


TEST_CASE( "SerialQueue: tasks run one at a time in submission order (ME-319)" )
{
    Jobs::SerialQueue queue( "Test Serial" );
    std::vector<int> order;
    std::atomic<int> concurrent{ 0 };
    std::atomic<bool> overlapped{ false };
    std::vector<std::future<void>> futures;
    for( int i = 0; i < 500; ++i )
    {
        futures.push_back( queue.Submit( [&, i]() {
            if( concurrent.fetch_add( 1 ) != 0 ) overlapped.store( true );
            order.push_back( i );   // unsynchronised on purpose: one at a time
            concurrent.fetch_sub( 1 );
        } ) );
    }
    queue.Flush();
    CHECK( queue.GetPending() == 0 );
    CHECK_FALSE( overlapped.load() );
    REQUIRE( order.size() == 500 );
    bool inOrder = true;
    for( int i = 0; i < 500; ++i ) inOrder = inOrder && order[i] == i;
    CHECK( inOrder );
    for( std::future<void>& future : futures ) CHECK( future.wait_for( std::chrono::seconds( 0 ) ) == std::future_status::ready );

    std::future<int> value = queue.Submit( []() { return 7; } );
    CHECK( value.get() == 7 );
}


TEST_CASE( "SerialQueue: Flush from a lane task with every lane thread busy runs the tasks itself (ME-319)" )
{
    Jobs::BackgroundLane& lane = Jobs::BackgroundLane::Get();
    std::atomic<bool> release{ false };
    std::atomic<int> blocked{ 0 };
    std::vector<std::future<void>> blockers;
    for( uint32_t i = 0; i + 1 < lane.GetThreadCount(); ++i )
    {
        blockers.push_back( lane.Submit( "Test Blocker", [&]() {
            blocked.fetch_add( 1 );
            while( !release.load() ) std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
        } ) );
    }
    REQUIRE( WaitUntil( [&]() { return blocked.load() == static_cast<int>( lane.GetThreadCount() ) - 1; } ) );

    // The last thread: queue work behind itself, then flush it. Its drain can't start (no free
    // thread), so Flush must run the tasks here.
    std::future<int> flushed = lane.Submit( "Test Flusher", []() {
        Jobs::SerialQueue queue( "Test Inner Serial" );
        int sum = 0;
        for( int i = 1; i <= 10; ++i ) queue.Submit( [&sum, i]() { sum += i; } );
        queue.Flush();
        return sum;
    } );
    REQUIRE( flushed.wait_for( std::chrono::seconds( 10 ) ) == std::future_status::ready );
    CHECK( flushed.get() == 55 );
    release.store( true );
    for( std::future<void>& blocker : blockers ) blocker.wait();
}


TEST_CASE( "SerialQueue: Clear drops the tasks that haven't started (ME-319)" )
{
    Jobs::SerialQueue queue( "Test Clear" );
    std::atomic<bool> release{ false };
    std::atomic<bool> started{ false };
    std::future<void> first = queue.Submit( [&]() {
        started.store( true );
        while( !release.load() ) std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
    } );
    REQUIRE( WaitUntil( [&]() { return started.load(); } ) );
    std::future<int> dropped = queue.Submit( []() { return 1; } );
    CHECK( queue.GetPending() == 2 );
    queue.Clear();
    CHECK( queue.GetPending() == 1 );
    release.store( true );
    queue.Flush();
    CHECK_THROWS_AS( dropped.get(), std::future_error );
}


TEST_CASE( "FrameStats: EndFrame reports the background lane next to the job workers (ME-319)" )
{
    Jobs::BackgroundLane& lane = Jobs::BackgroundLane::Get();
    FrameStats& stats = FrameStats::Get();
    stats.EndFrame( 16.0 );

    lane.Submit( "Test Frame Task", []() { SpinFor( std::chrono::milliseconds( 2 ) ); } ).wait();
    // The future is ready a moment before the task's thread books it: frames until it shows.
    uint32_t tasks = 0;
    double busy = 0.0;
    CHECK( WaitUntil( [&]() {
        stats.EndFrame( 16.0 );
        tasks += stats.GetJobStats().BackgroundJobs;
        busy += stats.GetJobStats().BackgroundBusyMilliseconds;
        return tasks > 0;
    } ) );

    const FrameStats::JobStats& jobStats = stats.GetJobStats();
    CHECK( jobStats.BackgroundCount == lane.GetThreadCount() );
    // At least this one (an earlier test's leftover serial-queue turn may land in the same frame).
    CHECK( tasks >= 1 );
    CHECK( busy >= 1.5 );
    uint32_t backgroundLanes = 0;
    for( const FrameStats::JobLane& entry : jobStats.Lanes ) backgroundLanes += entry.Kind == Jobs::LaneKind::Background ? 1 : 0;
    CHECK( backgroundLanes == lane.GetThreadCount() );

    const json workers = FrameStats::ToJson( jobStats, 16.0 );
    REQUIRE( workers.contains( "Background" ) );
    CHECK( workers["Background"]["Count"] == lane.GetThreadCount() );
    CHECK( workers["Background"]["Jobs"] == jobStats.BackgroundJobs );
    CHECK( workers["Background"]["BusyMs"].size() == lane.GetThreadCount() );
    CHECK( workers["Background"].contains( "Queued" ) );
}
