#include "JobSystem.h"
#include <mutex>
#include "optick.h"
#include <string>
#include <iostream>
#include <chrono>

JobSystem::JobSystem( std::size_t InNumThreads )
{
    WorkerThreads.resize( InNumThreads );

    std::size_t index = 0;
    for( auto& threadInfo : WorkerThreads )
    {
        threadInfo.IsWorking = false;
        threadInfo.Worker.SetJobSystem( this, index, m_isWorkAvailable );
    }

    index = 0;
    for( auto& threadInfo : WorkerThreads )
    {
        threadInfo.IsWorking = false;
        threadInfo.Worker.Start();
    }
}

JobSystem::~JobSystem()
{
}

const JobQueueOld& JobSystem::GetJobQueue() const
{
    return Queue;
}

JobQueueOld& JobSystem::GetJobQueue()
{
    return Queue;
}

void JobSystem::WorkerThreadActive( std::size_t InIndex )
{
    OPTICK_CATEGORY( "Worker Wake", Optick::Category::Debug );
    std::scoped_lock<std::mutex> lock( FinishedMutex );

    WorkerThreads[InIndex].IsWorking = true;
    --NumberOfSleepingThreads;
}

void JobSystem::WorkerThreadSleeping( std::size_t InIndex )
{
    OPTICK_CATEGORY( "Worker Sleep", Optick::Category::Debug );
    std::scoped_lock<std::mutex> lock( FinishedMutex );

    WorkerThreads[InIndex].IsWorking = false;
    ++NumberOfSleepingThreads;

    if( !Queue.HasWork() && NumberOfSleepingThreads == WorkerThreads.size() )
    {
        m_isWorkFinished = true;
    }
    if( !Queue.HasWork() )
    {
        m_isWorkAvailable = false;
    }
}

void JobSystem::AddWork( std::function<void()> func, bool signalNewWork )
{
    GetJobQueue().Push( func );
    if( signalNewWork )
    {
        SignalWorkAvailable();
    }
}

void JobSystem::SignalWorkAvailable()
{
    m_isWorkFinished = false;

    m_isWorkAvailable = true;
}

void JobSystem::Wait()
{
    OPTICK_CATEGORY( "Job System Wait", Optick::Category::Wait );
    //1fps
    //while (true)
    //{
    //    std::scoped_lock<std::mutex> lock( FinishedMutex );
    //
    //    if( !GetJobQueue().HasWork() )
    //    {
    //        return;
    //    }
    //
    //    std::this_thread::sleep_for( std::chrono::microseconds( 100 ) );
    //}
    while( true )
    {
        bool working = true;
        for( auto& it : WorkerThreads )
        {
            working = working && it.IsWorking;
        }
        if( !working && !m_isWorkAvailable && m_isWorkFinished )
        {
            return;
        }
    }
    while( !m_isWorkFinished )
    {
        //std::cout << std::to_string(NumberOfSleepingThreads);

        std::this_thread::sleep_for( std::chrono::microseconds( 100 ) );
    }
}
