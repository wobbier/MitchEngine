#pragma once
#include "JobQueue.h"
#include "WorkerThread.h"
#include <atomic>

struct ThreadInfo
{
	WorkerThread Worker;
	bool IsWorking = false;
};

class JobSystem
{
public:
	JobSystem(std::size_t InNumThreads);
	~JobSystem();

	const JobQueue& GetJobQueue() const;
	JobQueue& GetJobQueue();

	void WorkerThreadActive(std::size_t InIndex);
	void WorkerThreadSleeping(std::size_t InIndex);

	void Wait();
private:
	JobQueue Queue;

	std::vector<ThreadInfo> WorkerThreads;

	HANDLE WorkAvailableEvent;
	HANDLE WorkFinishedEvent;

	std::mutex FinishedMutex;
	std::atomic_size_t NumberOfSleepingThreads = 0;
};