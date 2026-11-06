#pragma once
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
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

    // Who ran jobs in a frame (ME-320). Lane 0 is the main thread (and any other thread that isn't a
    // worker: they share it), running ParallelFor's first batch and jobs it picks up while waiting;
    // lanes 1..N are Job Worker 1..N. A lane kind per entry so another pool (the ME-319 background
    // lane) can report its own lanes next to these.
    enum class LaneKind : uint8_t
    {
        Main,
        Worker,
    };

    struct LaneUtilization
    {
        LaneKind Kind = LaneKind::Worker;
        const char* Name = "";             // stable until the job system shuts down
        uint64_t BusyNanoseconds = 0;      // wall time running jobs (nested jobs aren't counted twice)
        uint32_t Jobs = 0;                 // jobs run, nested ones included
    };

    struct Utilization
    {
        std::vector<LaneUtilization> Lanes;
        // Time the main thread spent inside Wait (ParallelFor's included) with nothing to run: it was
        // waiting for jobs other threads were running. Waits inside a job aren't counted.
        uint64_t WaitNanoseconds = 0;
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
        // InName labels the job's profiler zone (a string literal: it's kept by pointer).
        template<typename Fn>
        void Run( Counter& counter, Fn&& fn, const char* InName = "Job" );

        // Blocks until the counter reaches zero, executing queued jobs meanwhile.
        void Wait( Counter& counter );

        // Calls fn( begin, end ) over [0, count) split into batches of at least minBatchSize, on all
        // threads including the caller. Returns when every batch is done. No allocations.
        template<typename Fn>
        void ParallelFor( uint32_t count, uint32_t minBatchSize, Fn&& fn, const char* InName = "Job" );
        // The same, its jobs' profiler zones named (a string literal: it's kept by pointer).
        template<typename Fn>
        void ParallelFor( const char* InName, uint32_t count, uint32_t minBatchSize, Fn&& fn )
        {
            ParallelFor( count, minBatchSize, std::forward<Fn>( fn ), InName );
        }

        // Process-wide instance without starting it (Get() starts it).
        static JobSystem& GetUnstarted();

        // Copies the utilization counted since the previous call into OutUtilization and zeroes the
        // counters (FrameStats::EndFrame calls it once a frame on the main thread). Empty lanes when
        // the system isn't running. Reuses OutUtilization's storage: no allocations once sized.
        void TakeUtilization( Utilization& OutUtilization );

        // Low level: enqueue a raw range job (used by physics task callbacks).
        using RangeFunction = void ( * )( void* context, uint32_t begin, uint32_t end );
        void Submit( Counter& counter, RangeFunction function, void* context, uint32_t begin, uint32_t end, const char* InName = "Job" );

    private:
        struct Job
        {
            RangeFunction Function = nullptr;
            void* Context = nullptr;
            void ( *DeleteContext )( void* context ) = nullptr;
            uint32_t Begin = 0;
            uint32_t End = 0;
            Counter* Owner = nullptr;
            const char* Name = "Job";
        };

        struct alignas( 64 ) Queue
        {
            std::mutex Lock;
            std::deque<Job> Jobs;
        };

        // One per lane (thread index), written with relaxed atomics: a worker writes only its own.
        struct alignas( 64 ) LaneCounters
        {
            std::atomic<uint64_t> BusyNanoseconds{ 0 };
            std::atomic<uint32_t> Jobs{ 0 };
        };

        // Counts one job body run on this thread for its lane: a job, and its time unless it runs
        // inside another job on this thread (then the outer one's time already covers it).
        class JobTimer
        {
        public:
            explicit JobTimer( JobSystem& InSystem );
            ~JobTimer();
            JobTimer( const JobTimer& ) = delete;
            JobTimer& operator=( const JobTimer& ) = delete;

        private:
            JobSystem& m_system;
            int64_t m_start = 0;
            bool m_outermost = false;
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
        std::unique_ptr<LaneCounters[]> m_lanes;
        std::vector<std::string> m_laneNames;
        std::atomic<uint64_t> m_waitNanoseconds{ 0 };
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
    void JobSystem::Run( Counter& counter, Fn&& fn, const char* InName )
    {
        using Callable = std::decay_t<Fn>;
        Job job;
        job.Context = new Callable( std::forward<Fn>( fn ) );
        job.Function = []( void* context, uint32_t, uint32_t ) { ( *static_cast<Callable*>( context ) )(); };
        job.DeleteContext = []( void* context ) { delete static_cast<Callable*>( context ); };
        job.Owner = &counter;
        job.Name = InName;
        counter.Pending.fetch_add( 1, std::memory_order_relaxed );
        Push( &job, 1 );
    }


    template<typename Fn>
    void JobSystem::ParallelFor( uint32_t count, uint32_t minBatchSize, Fn&& fn, const char* InName )
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
            job.Name = InName;
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

        {
            JobTimer timer( *this );
            fn( 0u, batchSize < count ? batchSize : count );
        }
        Wait( counter );
    }
}
