#include "JobEngine.h"
#include <atomic>

JobEngine::JobEngine( std::size_t InNumThreads, std::size_t InJobsPerThread )
    : Workers( InNumThreads )
{
    std::size_t jobsPerQueue = InJobsPerThread;
    Workers.EmplaceBack( this, jobsPerQueue, Worker::Mode::Foreground );

    for( std::size_t i = 1; i < InNumThreads; ++i )
    {
        Workers.EmplaceBack( this, jobsPerQueue, Worker::Mode::Background );
    }

    for( std::size_t i = 0; i < Workers.CurrentSize(); ++i )
    {
        Workers[i].Start();
    }
}

JobEngine::~JobEngine()
{
    for( std::size_t i = 1; i < Workers.CurrentSize(); ++i )
    {
        Workers[i].Stop();
    }
}

Worker* JobEngine::GetRandomWorker()
{
    const std::size_t count = Workers.CurrentSize();
    std::uniform_int_distribution<std::size_t> dist { 0, count - 1 };
    const std::size_t start = dist( RandomEngine );

    for( std::size_t i = 0; i < count; ++i )
    {
        Worker* worker = &Workers[( start + i ) % count];
        if( worker->IsRunning() )
        {
            return worker;
        }
    }

    return nullptr;
}

Worker* JobEngine::GetThreadWorker()
{
    return FindThreadWorker( std::this_thread::get_id() );
}

void JobEngine::ClearWorkerPools()
{
    for( std::size_t i = 0; i < Workers.CurrentSize(); ++i )
    {
        Workers[i].GetPool().Clear();
        Workers[i].Clear();
    }
}

Worker* JobEngine::FindThreadWorker( const std::thread::id InThreadId )
{
    for( std::size_t i = 0; i < Workers.CurrentSize(); ++i )
    {
        if( Workers[i].GetThreadId() == InThreadId )
        {
            return &Workers[i];
        }
    }
    return nullptr;
}

