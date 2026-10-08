#include <doctest/doctest.h>

#include "Jobs/JobSystem.h"
#include <atomic>
#include <numeric>
#include <vector>

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
