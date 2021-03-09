#include "JobSystem.h"
#include <synchapi.h>
#include <wtypes.h>
#include <mutex>

JobSystem::JobSystem(std::size_t InNumThreads)
{
	WorkerThreads.resize(InNumThreads);
	std::atomic_bool b = false;
	WorkAvailableEvent = CreateEvent(NULL, TRUE, FALSE, L"SignalWorkAvailable");
	WorkFinishedEvent = CreateEvent(NULL, TRUE, FALSE, L"WorkFinishedEvent");

	std::size_t index = 0;
	for (auto& threadInfo : WorkerThreads)
	{
		threadInfo.IsWorking = false;
		threadInfo.Worker.SetJobSystem(this, index, WorkAvailableEvent);
	}

	ResetEvent(WorkAvailableEvent);
	ResetEvent(WorkFinishedEvent);

	index = 0;
	for (auto& threadInfo : WorkerThreads)
	{
		threadInfo.IsWorking = false;
		threadInfo.Worker.Start();
	}
}

JobSystem::~JobSystem()
{

}

const JobQueue& JobSystem::GetJobQueue() const
{
	return Queue;
}

JobQueue& JobSystem::GetJobQueue()
{
	return Queue;
}

void JobSystem::WorkerThreadActive(std::size_t InIndex)
{
	std::scoped_lock<std::mutex> lock(FinishedMutex);

	WorkerThreads[InIndex].IsWorking = true;
	--NumberOfSleepingThreads;
}

void JobSystem::WorkerThreadSleeping(std::size_t InIndex)
{
	std::scoped_lock<std::mutex> lock(FinishedMutex);

	WorkerThreads[InIndex].IsWorking = true;
	++NumberOfSleepingThreads;

	if (!Queue.HasWork() && NumberOfSleepingThreads == WorkerThreads.size())
	{
		SetEvent(WorkFinishedEvent);
	}
	if (!Queue.HasWork())
	{
		ResetEvent(WorkAvailableEvent);
	}
}

void JobSystem::AddJobBrad(std::function<void()> func)
{
	GetJobQueue().AddJobBrad(func);
	SignalWorkAvailable();
}

void JobSystem::SignalWorkAvailable()
{
	ResetEvent(WorkFinishedEvent);
	SetEvent(WorkAvailableEvent);
}

void JobSystem::Wait()
{
	WaitForSingleObject(WorkFinishedEvent, INFINITE);
}
