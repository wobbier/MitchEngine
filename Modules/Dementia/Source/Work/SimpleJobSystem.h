#pragma once
#include <iostream>
#include <functional>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <vector>
#include <memory>
#include "optick.h"


class SimpleJobSystem
{
public:
    using SimpleJob = std::function<void()>;

    SimpleJobSystem( size_t numThreads = std::thread::hardware_concurrency() ) : shutdown( false ), activeJobs( 0 )
    {
        for( size_t i = 0; i < numThreads; ++i )
        {
            workers.emplace_back( [this]
                {
                    workerThread();
                } );
        }
    }

    ~SimpleJobSystem()
    {
        {
            std::lock_guard<std::mutex> lock( queueMutex );
            shutdown = true;
        }
        queueCondition.notify_all();
        for( auto& thread : workers )
        {
            if( thread.joinable() )
            {
                thread.join();
            }
        }
    }

    std::shared_ptr<std::atomic<bool>> submit( SimpleJob job )
    {
        auto isDone = std::make_shared<std::atomic<bool>>( false );
        {
            std::lock_guard<std::mutex> lock( queueMutex );
            jobQueue.emplace( [job, isDone, this]
                {
                    activeJobs.fetch_add( 1, std::memory_order_relaxed );
                    job();
                    *isDone = true;
                    activeJobs.fetch_sub( 1, std::memory_order_relaxed );
                    jobDoneCondition.notify_all();
                } );
        }
        queueCondition.notify_one();
        return isDone;
    }

    void waitForAllJobs( bool allowMainThreadWork = true )
    {
        while( true )
        {
            {
                std::lock_guard<std::mutex> lock( queueMutex );
                if( jobQueue.empty() && activeJobs.load( std::memory_order_acquire ) == 0 )
                {
                    break;
                }
            }

            if( allowMainThreadWork )
            {
                if( !workOnJob() )
                {
                    std::this_thread::yield();
                }
            }
            else
            {
                std::unique_lock<std::mutex> lock( jobDoneMutex );
                jobDoneCondition.wait( lock, [this]
                    {
                        return jobQueue.empty() && activeJobs.load( std::memory_order_acquire ) == 0;
                    } );
            }
        }
    }

private:
    std::vector<std::thread> workers;
    std::queue<SimpleJob> jobQueue;
    std::mutex queueMutex;
    std::condition_variable queueCondition;

    std::mutex jobDoneMutex;
    std::condition_variable jobDoneCondition;
    std::atomic<int> activeJobs;
    std::atomic<bool> shutdown;

    void workerThread()
    {
        OPTICK_THREAD( "Simple Worker Thread" );
        while( true )
        {
            SimpleJob job;
            {
                std::unique_lock<std::mutex> lock( queueMutex );
                queueCondition.wait( lock, [this]
                    {
                        return shutdown || !jobQueue.empty();
                    } );

                if( shutdown && jobQueue.empty() )
                {
                    return;
                }

                job = std::move( jobQueue.front() );
                jobQueue.pop();
            }
            activeJobs.fetch_add( 1, std::memory_order_relaxed );
            job();
            activeJobs.fetch_sub( 1, std::memory_order_relaxed );
            jobDoneCondition.notify_all();
        }
    }

    bool workOnJob()
    {
        SimpleJob job;
        {
            std::lock_guard<std::mutex> lock( queueMutex );
            if( jobQueue.empty() )
            {
                return false;
            }
            job = std::move( jobQueue.front() );
            jobQueue.pop();
        }
        activeJobs.fetch_add( 1, std::memory_order_relaxed );
        job();
        activeJobs.fetch_sub( 1, std::memory_order_relaxed );
        jobDoneCondition.notify_all();
        return true;
    }
};
