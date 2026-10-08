#include "JobSystem.h"
#include "optick.h"
#include <chrono>
#include <string>

namespace Jobs
{
    namespace
    {
        thread_local uint32_t s_threadIndex = 0;
    }


    JobSystem& JobSystem::Get()
    {
        static JobSystem instance;
        instance.EnsureStarted();
        return instance;
    }


    JobSystem::~JobSystem()
    {
        Shutdown();
    }


    uint32_t JobSystem::GetCurrentThreadIndex()
    {
        return s_threadIndex;
    }


    void JobSystem::EnsureStarted()
    {
        if( !m_started.load( std::memory_order_acquire ) )
        {
            Init( 0 );
        }
    }


    void JobSystem::Init( uint32_t workerCount )
    {
        std::lock_guard<std::mutex> lock( m_startMutex );
        if( m_started.load( std::memory_order_acquire ) )
        {
            return;
        }

        if( workerCount == 0 )
        {
            const uint32_t hardware = std::thread::hardware_concurrency();
            workerCount = hardware > 1 ? hardware - 1 : 1;
        }

        m_queueCount = workerCount + 1;
        m_queues.reset( new Queue[m_queueCount] );
        m_running.store( true, std::memory_order_release );

        m_workers.reserve( workerCount );
        for( uint32_t i = 0; i < workerCount; ++i )
        {
            m_workers.emplace_back( [this, i]() { WorkerMain( i + 1 ); } );
        }
        m_started.store( true, std::memory_order_release );
    }


    void JobSystem::Shutdown()
    {
        std::lock_guard<std::mutex> lock( m_startMutex );
        if( !m_started.load( std::memory_order_acquire ) )
        {
            return;
        }

        {
            std::lock_guard<std::mutex> sleepLock( m_sleepMutex );
            m_running.store( false, std::memory_order_release );
        }
        m_sleepCondition.notify_all();
        for( std::thread& worker : m_workers )
        {
            if( worker.joinable() )
            {
                worker.join();
            }
        }
        m_workers.clear();

        // Drop anything that was never run.
        for( uint32_t i = 0; i < m_queueCount; ++i )
        {
            for( Job& job : m_queues[i].Jobs )
            {
                if( job.DeleteContext )
                {
                    job.DeleteContext( job.Context );
                }
                if( job.Owner )
                {
                    job.Owner->Pending.fetch_sub( 1, std::memory_order_release );
                }
            }
            m_queues[i].Jobs.clear();
        }
        m_queuedJobs.store( 0 );
        m_started.store( false, std::memory_order_release );
    }


    void JobSystem::Submit( Counter& counter, RangeFunction function, void* context, uint32_t begin, uint32_t end )
    {
        EnsureStarted();
        Job job;
        job.Function = function;
        job.Context = context;
        job.Begin = begin;
        job.End = end;
        job.Owner = &counter;
        counter.Pending.fetch_add( 1, std::memory_order_relaxed );
        Push( &job, 1 );
    }


    void JobSystem::Push( const Job* jobs, size_t count )
    {
        EnsureStarted();
        if( m_workers.empty() )
        {
            // No workers (shut down): run inline so callers never hang.
            for( size_t i = 0; i < count; ++i )
            {
                Job job = jobs[i];
                Execute( job );
            }
            return;
        }

        Queue& queue = m_queues[s_threadIndex < m_queueCount ? s_threadIndex : 0];
        {
            std::lock_guard<std::mutex> lock( queue.Lock );
            queue.Jobs.insert( queue.Jobs.end(), jobs, jobs + count );
        }
        m_queuedJobs.fetch_add( static_cast<int32_t>( count ), std::memory_order_release );

        if( m_sleepingWorkers.load( std::memory_order_acquire ) > 0 )
        {
            if( count > 1 )
            {
                m_sleepCondition.notify_all();
            }
            else
            {
                m_sleepCondition.notify_one();
            }
        }
    }


    bool JobSystem::TryPop( uint32_t threadIndex, Job& outJob )
    {
        if( m_queuedJobs.load( std::memory_order_acquire ) <= 0 )
        {
            return false;
        }

        // Own queue first, newest job first (cache-warm).
        {
            Queue& own = m_queues[threadIndex];
            std::lock_guard<std::mutex> lock( own.Lock );
            if( !own.Jobs.empty() )
            {
                outJob = own.Jobs.back();
                own.Jobs.pop_back();
                m_queuedJobs.fetch_sub( 1, std::memory_order_acq_rel );
                return true;
            }
        }

        // Steal the oldest job from someone else.
        for( uint32_t offset = 1; offset < m_queueCount; ++offset )
        {
            Queue& victim = m_queues[( threadIndex + offset ) % m_queueCount];
            std::unique_lock<std::mutex> lock( victim.Lock, std::try_to_lock );
            if( !lock.owns_lock() )
            {
                // Busy; try it again on the next pass rather than contend.
                continue;
            }
            if( !victim.Jobs.empty() )
            {
                outJob = victim.Jobs.front();
                victim.Jobs.pop_front();
                m_queuedJobs.fetch_sub( 1, std::memory_order_acq_rel );
                return true;
            }
        }
        return false;
    }


    void JobSystem::Execute( Job& job )
    {
        job.Function( job.Context, job.Begin, job.End );
        if( job.DeleteContext )
        {
            job.DeleteContext( job.Context );
        }
        if( job.Owner )
        {
            job.Owner->Pending.fetch_sub( 1, std::memory_order_release );
        }
    }


    bool JobSystem::TryRunOne( uint32_t threadIndex )
    {
        Job job;
        if( !TryPop( threadIndex, job ) )
        {
            return false;
        }
        Execute( job );
        return true;
    }


    void JobSystem::Wait( Counter& counter )
    {
        const uint32_t threadIndex = s_threadIndex < m_queueCount ? s_threadIndex : 0;
        int idleSpins = 0;
        while( !counter.IsDone() )
        {
            if( m_queueCount > 0 && TryRunOne( threadIndex ) )
            {
                idleSpins = 0;
                continue;
            }
            // The remaining jobs are running on other threads.
            if( ++idleSpins < 64 )
            {
                std::this_thread::yield();
            }
            else
            {
                std::this_thread::sleep_for( std::chrono::microseconds( 50 ) );
            }
        }
    }


    void JobSystem::WorkerMain( uint32_t threadIndex )
    {
        s_threadIndex = threadIndex;
        const std::string threadName = "Job Worker " + std::to_string( threadIndex );
        OPTICK_THREAD( threadName.c_str() );

        int idleSpins = 0;
        while( m_running.load( std::memory_order_acquire ) )
        {
            if( TryRunOne( threadIndex ) )
            {
                idleSpins = 0;
                continue;
            }

            if( ++idleSpins < 256 )
            {
                std::this_thread::yield();
                continue;
            }

            // Nothing to do for a while: sleep until work arrives.
            std::unique_lock<std::mutex> lock( m_sleepMutex );
            m_sleepingWorkers.fetch_add( 1, std::memory_order_acq_rel );
            m_sleepCondition.wait_for( lock, std::chrono::milliseconds( 5 ), [this]() {
                return !m_running.load( std::memory_order_acquire ) || m_queuedJobs.load( std::memory_order_acquire ) > 0;
            } );
            m_sleepingWorkers.fetch_sub( 1, std::memory_order_acq_rel );
            idleSpins = 0;
        }
    }
}
