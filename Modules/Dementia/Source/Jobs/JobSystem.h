#pragma once
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#include "Core/ISystem.h"

namespace Jobs
{
    // Tracks outstanding jobs. JobSystem::Wait() runs other jobs while it waits, so waiting from a
    // worker (nested parallelism) never deadlocks.
    struct Counter
    {
        std::atomic<int32_t> Pending{ 0 };

        bool IsDone() const { return Pending.load( std::memory_order_acquire ) == 0; }
    };

    // Work-stealing job system: one queue per thread (callers that aren't workers share queue 0),
    // workers pop their own queue newest-first and steal oldest-first from the others, and sleep
    // when there is nothing to do.
    class JobSystem
        : public ISystem
    {
    public:
        ME_SYSTEM_ID( JobSystem );

        // Process-wide instance, started on first use with (hardware threads - 1) workers.
        static JobSystem& Get();

        JobSystem() = default;
        ~JobSystem();
        JobSystem( const JobSystem& ) = delete;
        JobSystem& operator=( const JobSystem& ) = delete;

        // workerCount 0 = hardware threads - 1 (at least 1).
        void Init( uint32_t workerCount = 0 );
        void Shutdown();

        uint32_t GetWorkerCount() const { return static_cast<uint32_t>( m_workers.size() ); }
        // Workers plus the calling thread: the useful amount of parallelism.
        uint32_t GetThreadCount() const { return GetWorkerCount() + 1; }

        // 0 on threads that aren't workers (e.g. the main thread), 1..N on workers.
        static uint32_t GetCurrentThreadIndex();

        // Runs fn() on some thread. The counter is incremented now and decremented when fn returns.
        template<typename Fn>
        void Run( Counter& counter, Fn&& fn );

        // Blocks until the counter reaches zero, executing queued jobs meanwhile.
        void Wait( Counter& counter );

        // Calls fn( begin, end ) over [0, count) split into batches of at least minBatchSize, on all
        // threads including the caller. Returns when every batch is done. No allocations.
        template<typename Fn>
        void ParallelFor( uint32_t count, uint32_t minBatchSize, Fn&& fn );

        // Low level: enqueue a raw range job (used by physics task callbacks).
        using RangeFunction = void ( * )( void* context, uint32_t begin, uint32_t end );
        void Submit( Counter& counter, RangeFunction function, void* context, uint32_t begin, uint32_t end );

    private:
        struct Job
        {
            RangeFunction Function = nullptr;
            void* Context = nullptr;
            void ( *DeleteContext )( void* context ) = nullptr;
            uint32_t Begin = 0;
            uint32_t End = 0;
            Counter* Owner = nullptr;
        };

        struct alignas( 64 ) Queue
        {
            std::mutex Lock;
            std::deque<Job> Jobs;
        };

        void Push( const Job* jobs, size_t count );
        bool TryRunOne( uint32_t threadIndex );
        bool TryPop( uint32_t threadIndex, Job& outJob );
        void Execute( Job& job );
        void WorkerMain( uint32_t threadIndex );
        void EnsureStarted();

        std::vector<std::thread> m_workers;
        std::unique_ptr<Queue[]> m_queues;
        uint32_t m_queueCount = 0;
        std::atomic<int32_t> m_queuedJobs{ 0 };
        std::atomic<bool> m_running{ false };
        std::atomic<bool> m_started{ false };
        std::mutex m_startMutex;

        std::mutex m_sleepMutex;
        std::condition_variable m_sleepCondition;
        std::atomic<int32_t> m_sleepingWorkers{ 0 };
    };

    // ---------------------------------------------------------------------------------------------

    template<typename Fn>
    void JobSystem::Run( Counter& counter, Fn&& fn )
    {
        using Callable = std::decay_t<Fn>;
        Job job;
        job.Context = new Callable( std::forward<Fn>( fn ) );
        job.Function = []( void* context, uint32_t, uint32_t ) { ( *static_cast<Callable*>( context ) )(); };
        job.DeleteContext = []( void* context ) { delete static_cast<Callable*>( context ); };
        job.Owner = &counter;
        counter.Pending.fetch_add( 1, std::memory_order_relaxed );
        Push( &job, 1 );
    }


    template<typename Fn>
    void JobSystem::ParallelFor( uint32_t count, uint32_t minBatchSize, Fn&& fn )
    {
        if( count == 0 )
        {
            return;
        }
        EnsureStarted();

        minBatchSize = minBatchSize == 0 ? 1 : minBatchSize;
        // A few batches per thread smooths out uneven work.
        const uint32_t targetBatches = GetThreadCount() * 4;
        uint32_t batchSize = ( count + targetBatches - 1 ) / targetBatches;
        batchSize = batchSize < minBatchSize ? minBatchSize : batchSize;
        const uint32_t batchCount = ( count + batchSize - 1 ) / batchSize;

        if( batchCount <= 1 || m_workers.empty() )
        {
            fn( 0u, count );
            return;
        }

        using Callable = std::remove_reference_t<Fn>;
        Callable* callable = &fn;
        auto invoke = []( void* context, uint32_t begin, uint32_t end ) { ( *static_cast<Callable*>( context ) )( begin, end ); };

        Counter counter;
        counter.Pending.store( static_cast<int32_t>( batchCount - 1 ), std::memory_order_relaxed );

        // Queue every batch but the first, which this thread runs right away.
        constexpr uint32_t kChunk = 64;
        Job jobs[kChunk];
        uint32_t queued = 0;
        for( uint32_t batch = 1; batch < batchCount; ++batch )
        {
            Job& job = jobs[queued++];
            job = Job();
            job.Function = invoke;
            job.Context = const_cast<void*>( static_cast<const void*>( callable ) );
            job.Begin = batch * batchSize;
            job.End = ( batch + 1 ) * batchSize < count ? ( batch + 1 ) * batchSize : count;
            job.Owner = &counter;
            if( queued == kChunk )
            {
                Push( jobs, queued );
                queued = 0;
            }
        }
        if( queued > 0 )
        {
            Push( jobs, queued );
        }

        fn( 0u, batchSize < count ? batchSize : count );
        Wait( counter );
    }
}
