#pragma once
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <exception>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#include "Jobs/JobSystem.h"

namespace Jobs
{
    // Which of the background lane's queues a task goes to (ME-319).
    enum class BackgroundPriority : uint8_t
    {
        // Latency matters: scene reads, save and replay writes, terrain pages. Always has a thread.
        Normal,
        // Long CPU work: navmesh builds, terrain collision cooks, texture packing. At most
        // (threads - 1) run at once, so a navmesh rebuild never holds up a scene read or a write.
        Low,
    };

    // The engine's one pool for background work (ME-319): a few named threads ("Background N",
    // hardware threads / 4, 2..4) next to the job workers, below normal OS priority. Frame jobs never
    // wait on it (JobSystem::Wait doesn't run its tasks), and its tasks never run frame jobs, so a
    // long task can't stretch a frame. Submit returns a std::future; cancellation is the caller's
    // token checked inside the task (terrain epochs, nav Cancel flags). Never block a lane task on
    // another lane task's future: use ParallelFor, which the caller helps with, or a SerialQueue.
    class BackgroundLane
    {
    public:
        // Process-wide instance, started on first use (and not again after Shutdown).
        static BackgroundLane& Get();
        // Process-wide instance without starting it.
        static BackgroundLane& GetUnstarted();
        // hardware threads / 4, clamped to 2..4.
        static uint32_t DefaultThreadCount();

        BackgroundLane() = default;
        ~BackgroundLane();
        BackgroundLane( const BackgroundLane& ) = delete;
        BackgroundLane& operator=( const BackgroundLane& ) = delete;

        // InThreadCount 0 = DefaultThreadCount().
        void Init( uint32_t InThreadCount = 0 );
        // Runs everything already queued (save and replay writes must land), then joins. Tasks
        // submitted afterwards run inline on the caller.
        void Shutdown();

        bool IsRunning() const { return m_running.load( std::memory_order_acquire ); }
        uint32_t GetThreadCount() const { return static_cast<uint32_t>( m_threads.size() ); }
        // Tasks waiting for a thread (not the running ones).
        uint32_t GetQueuedCount() const;
        // 1..N on a lane thread, 0 elsewhere.
        static uint32_t GetCurrentThreadIndex();

        // Runs fn() on a lane thread; the future carries its result or exception. InName labels the
        // task's profiler zone (a string literal: kept by pointer).
        template<typename Fn>
        auto Submit( const char* InName, Fn&& fn, BackgroundPriority InPriority = BackgroundPriority::Normal )
            -> std::future<std::invoke_result_t<std::decay_t<Fn>&>>;

        // Fire and forget. An exception escaping fn is logged.
        template<typename Fn>
        void Post( const char* InName, Fn&& fn, BackgroundPriority InPriority = BackgroundPriority::Normal );

        // Calls fn( index ) for [0, InCount): the calling thread works through the indices and up to
        // InMaxHelpers lane threads (0 = all of them) help while they're free. Never deadlocks, even
        // from a lane task with every lane thread busy: the caller then does it all itself. Helpers
        // queue at the calling task's priority (Normal off the lane). Rethrows the first exception.
        template<typename Fn>
        void ParallelFor( const char* InName, uint32_t InCount, Fn&& fn, uint32_t InMaxHelpers = 0 );

        // Appends one LaneKind::Background lane per thread with the time it spent in tasks since the
        // previous call (a task still running counts up to now), and zeroes them. FrameStats calls
        // it once a frame, after JobSystem::TakeUtilization.
        void AppendUtilization( Utilization& InOutUtilization );

    private:
        struct TaskBody
        {
            virtual ~TaskBody() = default;
            virtual void Run() = 0;
        };

        template<typename Fn>
        struct TaskOf final : TaskBody
        {
            explicit TaskOf( Fn&& InFn ) : Callable( std::move( InFn ) ) {}
            void Run() override { Callable(); }
            Fn Callable;
        };

        struct Task
        {
            std::unique_ptr<TaskBody> Body;
            const char* Name = "Background Task";
            BackgroundPriority Priority = BackgroundPriority::Normal;
        };

        // Busy time per thread, under a lock: tasks are coarse, and a task still running when the
        // frame's numbers are taken counts up to then.
        struct alignas( 64 ) LaneCounters
        {
            std::mutex Lock;
            int64_t RunningSince = 0;
            uint64_t BusyNanoseconds = 0;
            uint32_t Jobs = 0;
        };

        void Push( Task&& InTask );
        void RunTask( Task& InTask );
        void ThreadMain( uint32_t InIndex );
        bool CanRun( BackgroundPriority InPriority ) const;
        void EnsureStarted();
        void ParallelForImpl( const char* InName, uint32_t InCount, void ( *InFn )( void*, uint32_t ), void* InContext, uint32_t InMaxHelpers );

        std::vector<std::thread> m_threads;
        std::vector<std::string> m_names;
        std::unique_ptr<LaneCounters[]> m_counters;
        uint32_t m_counterCount = 0;

        mutable std::mutex m_queueLock;
        std::condition_variable m_wake;
        std::deque<Task> m_normal;
        std::deque<Task> m_low;
        uint32_t m_runningLow = 0;
        uint32_t m_maxLow = 1;
        bool m_stopping = false;

        std::atomic<bool> m_running{ false };
        bool m_shutDown = false;   // Get() doesn't restart it after Shutdown (Init does)
        std::mutex m_startMutex;
    };

    // Runs tasks one at a time in submission order, on the background lane (a strand: it holds no
    // thread of its own). For ordered streams: a replay recorder hands each tick's buffer to one,
    // and the compress + write of tick N always lands before tick N+1's, without blocking the tick.
    class SerialQueue
    {
    public:
        explicit SerialQueue( const char* InName, BackgroundPriority InPriority = BackgroundPriority::Normal );
        // Flushes: everything submitted runs before the queue goes away.
        ~SerialQueue();
        SerialQueue( const SerialQueue& ) = delete;
        SerialQueue& operator=( const SerialQueue& ) = delete;

        template<typename Fn>
        auto Submit( Fn&& fn ) -> std::future<std::invoke_result_t<std::decay_t<Fn>&>>;

        // Blocks until everything submitted so far has run.
        void Flush();
        // Submitted and not finished (the running one included): back-pressure for a writer.
        uint32_t GetPending() const;
        // Drops the tasks that haven't started (their futures report broken_promise).
        void Clear();

    private:
        struct State;
        void Enqueue( std::unique_ptr<std::packaged_task<void()>> InTask );
        static void Schedule( const std::shared_ptr<State>& InState );

        std::shared_ptr<State> m_state;
    };

    // ---------------------------------------------------------------------------------------------

    template<typename Fn>
    auto BackgroundLane::Submit( const char* InName, Fn&& fn, BackgroundPriority InPriority )
        -> std::future<std::invoke_result_t<std::decay_t<Fn>&>>
    {
        using Result = std::invoke_result_t<std::decay_t<Fn>&>;
        std::packaged_task<Result()> task( std::forward<Fn>( fn ) );
        std::future<Result> future = task.get_future();
        Post( InName, std::move( task ), InPriority );
        return future;
    }


    template<typename Fn>
    void BackgroundLane::Post( const char* InName, Fn&& fn, BackgroundPriority InPriority )
    {
        using Callable = std::decay_t<Fn>;
        Task task;
        task.Body = std::make_unique<TaskOf<Callable>>( Callable( std::forward<Fn>( fn ) ) );
        task.Name = InName;
        task.Priority = InPriority;
        Push( std::move( task ) );
    }


    template<typename Fn>
    void BackgroundLane::ParallelFor( const char* InName, uint32_t InCount, Fn&& fn, uint32_t InMaxHelpers )
    {
        using Callable = std::remove_reference_t<Fn>;
        auto invoke = []( void* InContext, uint32_t InIndex ) { ( *static_cast<Callable*>( InContext ) )( InIndex ); };
        ParallelForImpl( InName, InCount, invoke, const_cast<void*>( static_cast<const void*>( &fn ) ), InMaxHelpers );
    }


    struct SerialQueue::State
    {
        mutable std::mutex Lock;
        std::condition_variable Idle;
        std::deque<std::unique_ptr<std::packaged_task<void()>>> Tasks;
        bool Scheduled = false;  // a turn is queued on the lane
        bool Executing = false;  // a thread (a lane turn or a Flush) is running its tasks
        uint32_t Running = 0;
        const char* Name = "Serial Queue";
        BackgroundPriority Priority = BackgroundPriority::Normal;
    };


    template<typename Fn>
    auto SerialQueue::Submit( Fn&& fn ) -> std::future<std::invoke_result_t<std::decay_t<Fn>&>>
    {
        using Result = std::invoke_result_t<std::decay_t<Fn>&>;
        auto task = std::make_shared<std::packaged_task<Result()>>( std::forward<Fn>( fn ) );
        std::future<Result> future = task->get_future();
        Enqueue( std::make_unique<std::packaged_task<void()>>( [task]() { ( *task )(); } ) );
        return future;
    }
}
