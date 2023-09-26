#include "Worker.h"
#include "Job.h"
#include "JobEngine.h"
#include <optick.h>
#include <CLog.h>

Worker::Worker( JobEngine* engine, std::size_t InMaxJobs, Mode InMode /*= Mode::Background*/ )
    : WorkPool( InMaxJobs )
    , queue( InMaxJobs )
    , ThreadMode( InMode )
    , jobEngine( engine )
{
}

Worker::~Worker()
{
}

void Worker::Start()
{
    IsThreadRunning = true;
    if( ThreadMode.load() == Mode::Foreground )
    {
        ThreadId = std::this_thread::get_id();
    }
    else
    {
        WorkerThread = std::thread( [this] {
            OPTICK_THREAD( "Burst Thread" );
            while( IsRunning() )
            {
                OPTICK_EVENT( "GetJob" )
                    Job* job = GetJob();
                if( job )
                {
                    job->Run();
                }
            }
            } );
        ThreadId = WorkerThread.get_id();
    }
}

void Worker::Stop()
{
    IsThreadRunning.store( false );
    WorkerThread.join();
    jobEngine = nullptr;
}

void Worker::Clear()
{
    queue.Clear();
}

bool Worker::IsRunning() const
{
    return IsThreadRunning.load();
}

Pool& Worker::GetPool()
{
    return WorkPool;
}

void Worker::Submit( Job* InJob )
{
    queue.Push( InJob );
}

void Worker::Wait( Job* InJob )
{
    OPTICK_EVENT( "Wait" )

        float failedAttempts = 0;
    while( !InJob->IsFinished() )
    {
        Job* job = GetJob();

        if( job )
        {
            job->Run();
        }
        else
        {
            // I don't like this
            failedAttempts++;
            if( failedAttempts > 50 )
            {
                InJob->Run();
                break;
            }
        }
    }
}

std::thread::id Worker::GetThreadId() const
{
    return ThreadId;
}

Job* Worker::GetJob()
{
    Job* job = queue.Pop();

    if( job == nullptr )
    {
        Worker* worker = jobEngine->GetRandomWorker();

        if( worker && worker != this )
        {
            Job* stolenJob = worker->queue.Steal();

            if( stolenJob )
            {
                return stolenJob;
            }
            else
            {
                std::this_thread::yield();
                return nullptr;
            }
        }
        else
        {
            std::this_thread::yield();
            return nullptr;
        }
    }

    return job;
}

