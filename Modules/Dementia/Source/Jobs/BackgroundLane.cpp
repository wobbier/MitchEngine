#include "BackgroundLane.h"
#include "CLog.h"
#include "Dementia.h"
#include "optick.h"
#include "Profiling/ThreadName.h"
#include <algorithm>
#include <chrono>

#if USING( ME_PLATFORM_WINDOWS )
#include <windows.h>
#elif USING( ME_PLATFORM_MACOS )
#include <pthread.h>
#include <pthread/qos.h>
#else
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace Jobs
{
    namespace
    {
        thread_local uint32_t s_laneIndex = 0;
        // The priority of the task running on this lane thread (ParallelFor's helpers inherit it).
        thread_local BackgroundPriority s_priority = BackgroundPriority::Normal;

        int64_t NowNanoseconds()
        {
            return std::chrono::duration_cast<std::chrono::nanoseconds>( std::chrono::steady_clock::now().time_since_epoch() ).count();
        }

        // Below the job workers and the main thread: when every core is busy, frame work wins.
        void LowerThreadPriority()
        {
#if USING( ME_PLATFORM_WINDOWS )
            SetThreadPriority( GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL );
#elif USING( ME_PLATFORM_MACOS )
            pthread_set_qos_class_self_np( QOS_CLASS_UTILITY, 0 );
#else
            // Linux: a thread's nice value is its own (setpriority on the thread id). Raising it
            // needs no privileges.
            const id_t thread = static_cast<id_t>( syscall( SYS_gettid ) );
            const int current = getpriority( PRIO_PROCESS, thread );
            setpriority( PRIO_PROCESS, thread, std::min( current + 5, 19 ) );
#endif
        }
    }


    BackgroundLane& BackgroundLane::GetUnstarted()
    {
        static BackgroundLane instance;
        return instance;
    }


    BackgroundLane& BackgroundLane::Get()
    {
        BackgroundLane& instance = GetUnstarted();
        instance.EnsureStarted();
        return instance;
    }


    uint32_t BackgroundLane::DefaultThreadCount()
    {
        return std::clamp( std::thread::hardware_concurrency() / 4u, 2u, 4u );
    }


    uint32_t BackgroundLane::GetCurrentThreadIndex()
    {
        return s_laneIndex;
    }


    BackgroundLane::~BackgroundLane()
    {
        Shutdown();
    }


    void BackgroundLane::EnsureStarted()
    {
        if( !m_running.load( std::memory_order_acquire ) )
        {
            {
                std::lock_guard<std::mutex> lock( m_startMutex );
                if( m_shutDown )
                {
                    return;
                }
            }
            Init( 0 );
        }
    }


    void BackgroundLane::Init( uint32_t InThreadCount )
    {
        std::lock_guard<std::mutex> lock( m_startMutex );
        if( m_running.load( std::memory_order_acquire ) )
        {
            return;
        }
        m_shutDown = false;
        const uint32_t count = InThreadCount == 0 ? DefaultThreadCount() : InThreadCount;
        {
            std::lock_guard<std::mutex> queueLock( m_queueLock );
            m_stopping = false;
            m_runningLow = 0;
            m_maxLow = count > 1 ? count - 1 : 1;
        }
        m_counterCount = count;
        m_counters.reset( new LaneCounters[count] );
        m_names.clear();
        for( uint32_t i = 1; i <= count; ++i )
        {
            m_names.push_back( "Background " + std::to_string( i ) );
        }
        m_threads.reserve( count );
        for( uint32_t i = 1; i <= count; ++i )
        {
            m_threads.emplace_back( [this, i]() { ThreadMain( i ); } );
        }
        m_running.store( true, std::memory_order_release );
    }


    void BackgroundLane::Shutdown()
    {
        std::lock_guard<std::mutex> lock( m_startMutex );
        if( !m_running.load( std::memory_order_acquire ) )
        {
            return;
        }
        m_shutDown = true;
        {
            std::lock_guard<std::mutex> queueLock( m_queueLock );
            m_stopping = true;
        }
        m_wake.notify_all();
        for( std::thread& thread : m_threads )
        {
            if( thread.joinable() )
            {
                thread.join();
            }
        }
        m_threads.clear();
        m_running.store( false, std::memory_order_release );
    }


    uint32_t BackgroundLane::GetQueuedCount() const
    {
        std::lock_guard<std::mutex> lock( m_queueLock );
        return static_cast<uint32_t>( m_normal.size() + m_low.size() );
    }


    void BackgroundLane::Push( Task&& InTask )
    {
        {
            std::lock_guard<std::mutex> lock( m_queueLock );
            if( m_running.load( std::memory_order_acquire ) && !m_stopping )
            {
                if( InTask.Priority == BackgroundPriority::Low )
                {
                    m_low.push_back( std::move( InTask ) );
                }
                else
                {
                    m_normal.push_back( std::move( InTask ) );
                    m_normalWaiting.store( static_cast<uint32_t>( m_normal.size() ), std::memory_order_release );
                }
            }
        }
        if( InTask.Body )
        {
            // Not running (never started, or shut down): run it here so callers never hang.
            RunTask( InTask );
            return;
        }
        m_wake.notify_one();
    }


    bool BackgroundLane::CanRun( BackgroundPriority InPriority ) const
    {
        return InPriority == BackgroundPriority::Normal ? !m_normal.empty() : !m_low.empty() && m_runningLow < m_maxLow;
    }


    bool BackgroundLane::CanRunHelper() const
    {
        // Behind Normal work; no Low slot: a helper only speeds up a loop whose caller already runs.
        return !m_helpers.empty() && m_normal.empty();
    }


    void BackgroundLane::RunTask( Task& InTask )
    {
        const uint32_t lane = s_laneIndex;
        LaneCounters* counters = lane > 0 && lane <= m_counterCount ? &m_counters[lane - 1] : nullptr;
        if( counters )
        {
            std::lock_guard<std::mutex> lock( counters->Lock );
            counters->RunningSince = NowNanoseconds();
        }
        const BackgroundPriority previous = s_priority;
        s_priority = InTask.Priority;
        {
            OPTICK_EVENT_DYNAMIC( InTask.Name ? InTask.Name : "Background Task" );
            try
            {
                InTask.Body->Run();
            }
            catch( const std::exception& e )
            {
                CLog::Log( CLog::LogType::Error, std::string( "Background task '" ) + ( InTask.Name ? InTask.Name : "?" ) + "' threw: " + e.what() );
            }
            catch( ... )
            {
                CLog::Log( CLog::LogType::Error, std::string( "Background task '" ) + ( InTask.Name ? InTask.Name : "?" ) + "' threw" );
            }
            InTask.Body.reset();
        }
        s_priority = previous;
        if( counters )
        {
            std::lock_guard<std::mutex> lock( counters->Lock );
            const int64_t now = NowNanoseconds();
            counters->BusyNanoseconds += static_cast<uint64_t>( std::max<int64_t>( now - counters->RunningSince, 0 ) );
            counters->RunningSince = 0;
            ++counters->Jobs;
        }
    }


    void BackgroundLane::ThreadMain( uint32_t InIndex )
    {
        s_laneIndex = InIndex;
        ME_PROFILE_THREAD( m_names[InIndex - 1].c_str() );
        LowerThreadPriority();

        std::unique_lock<std::mutex> lock( m_queueLock );
        for( ;; )
        {
            m_wake.wait( lock, [this]() {
                return CanRun( BackgroundPriority::Normal ) || CanRunHelper() || CanRun( BackgroundPriority::Low )
                    || ( m_stopping && m_normal.empty() && m_low.empty() && m_helpers.empty() );
            } );
            Task task;
            bool lowSlot = false;
            if( CanRun( BackgroundPriority::Normal ) )
            {
                task = std::move( m_normal.front() );
                m_normal.pop_front();
                m_normalWaiting.store( static_cast<uint32_t>( m_normal.size() ), std::memory_order_release );
            }
            else if( CanRunHelper() )
            {
                // Before new Low work: it finishes a loop that is already running.
                task = std::move( m_helpers.front() );
                m_helpers.pop_front();
            }
            else if( CanRun( BackgroundPriority::Low ) )
            {
                task = std::move( m_low.front() );
                m_low.pop_front();
                ++m_runningLow;
                lowSlot = true;
            }
            else
            {
                return;   // stopping, and everything queued has run
            }
            // Taking a Normal task can make queued helpers runnable: pass the wake on.
            if( CanRun( BackgroundPriority::Normal ) || CanRunHelper() || CanRun( BackgroundPriority::Low ) )
            {
                m_wake.notify_one();
            }
            lock.unlock();
            RunTask( task );
            lock.lock();
            if( lowSlot )
            {
                --m_runningLow;
                // A Low task that was held back can take this slot.
                if( !m_low.empty() )
                {
                    m_wake.notify_one();
                }
            }
        }
    }


    // A ParallelFor's loop, shared with its helpers: one that starts after the work is gone finds no
    // index left and never touches the caller's callable, which is only alive until every index is done.
    struct BackgroundLane::ParallelState
    {
        std::atomic<uint32_t> Next{ 0 };
        std::atomic<uint32_t> Done{ 0 };
        uint32_t Count = 0;
        void ( *Fn )( void*, uint32_t ) = nullptr;
        void* Context = nullptr;
        std::mutex Lock;
        std::condition_variable Finished;
        std::exception_ptr Error;

        // Runs indices until none are left. With InYieldWhen (a helper), stops early while it is
        // non-zero (Normal work waiting) and returns true if indices were left.
        bool Drain( const std::atomic<uint32_t>* InYieldWhen )
        {
            for( ;; )
            {
                if( InYieldWhen && InYieldWhen->load( std::memory_order_acquire ) > 0 )
                {
                    return Next.load( std::memory_order_acquire ) < Count;
                }
                const uint32_t index = Next.fetch_add( 1 );
                if( index >= Count )
                {
                    return false;
                }
                try
                {
                    Fn( Context, index );
                }
                catch( ... )
                {
                    std::lock_guard<std::mutex> lock( Lock );
                    if( !Error )
                    {
                        Error = std::current_exception();
                    }
                }
                if( Done.fetch_add( 1, std::memory_order_acq_rel ) + 1 == Count )
                {
                    std::lock_guard<std::mutex> lock( Lock );
                    Finished.notify_all();
                }
            }
        }
    };


    void BackgroundLane::ParallelForImpl( const char* InName, uint32_t InCount, void ( *InFn )( void*, uint32_t ), void* InContext, uint32_t InMaxHelpers )
    {
        if( InCount == 0 )
        {
            return;
        }
        OPTICK_EVENT_DYNAMIC( InName ? InName : "Background ParallelFor" );
        uint32_t helpers = IsRunning() ? std::min( GetThreadCount(), InCount - 1 ) : 0;
        if( InMaxHelpers > 0 )
        {
            helpers = std::min( helpers, InMaxHelpers );
        }
        if( helpers == 0 )
        {
            for( uint32_t i = 0; i < InCount; ++i )
            {
                InFn( InContext, i );
            }
            return;
        }

        auto state = std::make_shared<ParallelState>();
        state->Count = InCount;
        state->Fn = InFn;
        state->Context = InContext;

        const BackgroundPriority priority = s_laneIndex > 0 ? s_priority : BackgroundPriority::Normal;
        for( uint32_t i = 0; i < helpers; ++i )
        {
            PostHelper( InName, state, priority );
        }
        state->Drain( nullptr );
        {
            std::unique_lock<std::mutex> lock( state->Lock );
            state->Finished.wait( lock, [&state]() { return state->Done.load( std::memory_order_acquire ) == state->Count; } );
        }
        // Helpers that never started have nothing left to do: drop them so they don't sit in the
        // queue (they'd only find the loop finished).
        std::vector<Task> unstarted;
        {
            std::lock_guard<std::mutex> lock( m_queueLock );
            for( auto it = m_helpers.begin(); it != m_helpers.end(); )
            {
                if( it->Group == state.get() )
                {
                    unstarted.push_back( std::move( *it ) );
                    it = m_helpers.erase( it );
                }
                else
                {
                    ++it;
                }
            }
        }
        unstarted.clear();
        if( state->Error )
        {
            std::rethrow_exception( state->Error );
        }
    }


    void BackgroundLane::PostHelper( const char* InName, const std::shared_ptr<ParallelState>& InState, BackgroundPriority InPriority )
    {
        auto help = [this, InName, state = InState, InPriority]() {
            // Stepped aside for Normal work with indices left: queue again behind it.
            if( state->Drain( &m_normalWaiting ) )
            {
                PostHelper( InName, state, InPriority );
            }
        };
        Task task;
        task.Body = std::make_unique<TaskOf<decltype( help )>>( std::move( help ) );
        task.Name = InName;
        task.Priority = InPriority;
        task.Group = InState.get();
        {
            std::lock_guard<std::mutex> lock( m_queueLock );
            if( !m_running.load( std::memory_order_acquire ) || m_stopping )
            {
                return;   // the caller finishes the loop itself
            }
            m_helpers.push_back( std::move( task ) );
        }
        m_wake.notify_one();
    }


    void BackgroundLane::AppendUtilization( Utilization& InOutUtilization )
    {
        std::lock_guard<std::mutex> lock( m_startMutex );
        if( !m_running.load( std::memory_order_acquire ) )
        {
            return;
        }
        const int64_t now = NowNanoseconds();
        for( uint32_t i = 0; i < m_counterCount; ++i )
        {
            LaneCounters& counters = m_counters[i];
            LaneUtilization lane;
            lane.Kind = LaneKind::Background;
            lane.Name = m_names[i].c_str();
            {
                std::lock_guard<std::mutex> counterLock( counters.Lock );
                if( counters.RunningSince != 0 )
                {
                    counters.BusyNanoseconds += static_cast<uint64_t>( std::max<int64_t>( now - counters.RunningSince, 0 ) );
                    counters.RunningSince = now;
                }
                lane.BusyNanoseconds = counters.BusyNanoseconds;
                lane.Jobs = counters.Jobs;
                counters.BusyNanoseconds = 0;
                counters.Jobs = 0;
            }
            InOutUtilization.Lanes.push_back( lane );
        }
    }

    // --------------------------------------------------------------------------------- SerialQueue

    SerialQueue::SerialQueue( const char* InName, BackgroundPriority InPriority )
        : m_state( std::make_shared<State>() )
    {
        m_state->Name = InName;
        m_state->Priority = InPriority;
    }


    SerialQueue::~SerialQueue()
    {
        Flush();
    }


    namespace
    {
        // Runs up to InBudget of the queue's tasks in order (all of them for 0). Returns with the
        // state's lock held. Only one thread executes a queue's tasks at a time (Executing).
        template<typename StateT>
        void RunSerialTasks( StateT& InState, std::unique_lock<std::mutex>& InLock, uint32_t InBudget )
        {
            InState.Executing = true;
            for( uint32_t ran = 0; !InState.Tasks.empty() && ( InBudget == 0 || ran < InBudget ); ++ran )
            {
                std::unique_ptr<std::packaged_task<void()>> task = std::move( InState.Tasks.front() );
                InState.Tasks.pop_front();
                ++InState.Running;
                InLock.unlock();
                ( *task )();   // a packaged_task keeps exceptions in its future
                task.reset();
                InLock.lock();
                --InState.Running;
            }
            InState.Executing = false;
            InState.Idle.notify_all();
        }
    }


    void SerialQueue::Enqueue( std::unique_ptr<std::packaged_task<void()>> InTask )
    {
        bool schedule = false;
        {
            std::lock_guard<std::mutex> lock( m_state->Lock );
            m_state->Tasks.push_back( std::move( InTask ) );
            if( !m_state->Executing && !m_state->Scheduled )
            {
                m_state->Scheduled = schedule = true;
            }
        }
        if( schedule )
        {
            Schedule( m_state );
        }
    }


    void SerialQueue::Schedule( const std::shared_ptr<State>& InState )
    {
        BackgroundLane::Get().Post( InState->Name, [state = InState]() {
            std::unique_lock<std::mutex> lock( state->Lock );
            state->Scheduled = false;
            if( state->Executing )
            {
                return;   // a Flush is running them on its own thread
            }
            // A bounded turn, then back in line: a busy stream doesn't keep a lane thread forever.
            RunSerialTasks( *state, lock, 16 );
            if( !state->Tasks.empty() && !state->Scheduled )
            {
                state->Scheduled = true;
                lock.unlock();
                Schedule( state );
            }
        }, InState->Priority );
    }


    void SerialQueue::Flush()
    {
        std::unique_lock<std::mutex> lock( m_state->Lock );
        for( ;; )
        {
            if( !m_state->Executing )
            {
                if( m_state->Tasks.empty() )
                {
                    return;
                }
                // Nobody is running them (the drain may still be queued behind busy lane threads):
                // run them here, in order, rather than wait on the lane from what may be a lane thread.
                RunSerialTasks( *m_state, lock, 0 );
                continue;
            }
            m_state->Idle.wait( lock );
        }
    }


    uint32_t SerialQueue::GetPending() const
    {
        std::lock_guard<std::mutex> lock( m_state->Lock );
        return static_cast<uint32_t>( m_state->Tasks.size() ) + m_state->Running;
    }


    void SerialQueue::Clear()
    {
        std::deque<std::unique_ptr<std::packaged_task<void()>>> dropped;
        {
            std::lock_guard<std::mutex> lock( m_state->Lock );
            dropped.swap( m_state->Tasks );
        }
    }
}
