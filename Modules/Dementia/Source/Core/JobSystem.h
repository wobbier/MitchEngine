#pragma once
#include "JobQueueOld.h"
#include "WorkerThread.h"
#include <atomic>
#include "ISystem.h"

struct ThreadInfo
{
    WorkerThread Worker;
    bool IsWorking = false;
};

class JobSystem
    : public ISystem
{
public:
    ME_SYSTEM_ID( JobSystem );

    JobSystem( std::size_t InNumThreads );
    ~JobSystem();

    const JobQueueOld& GetJobQueue() const;
    JobQueueOld& GetJobQueue();

    void WorkerThreadActive( std::size_t InIndex );
    void WorkerThreadSleeping( std::size_t InIndex );

    void AddWork( std::function<void()> func, bool signalNewWork = true );

    void SignalWorkAvailable();

    void Wait();
private:
    JobQueueOld Queue;

    std::vector<ThreadInfo> WorkerThreads;

    //HANDLE WorkAvailableEvent;
    std::atomic_bool m_isWorkAvailable = false;
    //HANDLE WorkFinishedEvent;
    std::atomic_bool m_isWorkFinished = false;

    std::mutex FinishedMutex;
    std::atomic_size_t NumberOfSleepingThreads = 0;
};