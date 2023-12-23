#include "WorkerThread.h"
#include "JobSystem.h"
#include <optick.h>

void WorkerThread::SetJobSystem( JobSystem* InJobSystem, std::size_t InIndex, const std::atomic_bool& EventHandle )
{
    Owner = InJobSystem;
    Index = InIndex;
    WorkAvailableHandle = &EventHandle;
}

void WorkerThread::Start()
{
    Create( [this]() {
        OPTICK_THREAD( ThreadName.c_str() );
        while( IsAlive() )
        {
            if( IsPaused )
            {
                IsPaused = false;
                Owner->WorkerThreadActive( Index );
            }

            auto& queue = Owner->GetJobQueue();
            auto job = queue.GetNextJob();
            if( job )
            {
                OPTICK_CATEGORY( "Running Job", Optick::Category::Wait );
                job();
            }
            else
            {
                OPTICK_CATEGORY( "Setting Sleeping Job", Optick::Category::Wait );
                Owner->WorkerThreadSleeping( Index );
                Pause();
            }
        }
        }, "Job Thread" );
}

void WorkerThread::Pause()
{
    OPTICK_CATEGORY( "Pause", Optick::Category::Wait );
    IsPaused = true;

    while( !( *WorkAvailableHandle ) && IsAlive() )
    {
        std::this_thread::yield();
    }
}
