#include "JobSystem.h"
#include <mutex>

JobSystem::JobSystem(std::size_t InNumThreads)
{
	WorkerThreads.resize(InNumThreads);

	std::size_t index = 0;
	for (auto& threadInfo : WorkerThreads)
	{
		threadInfo.IsWorking = false;
		threadInfo.Worker.SetJobSystem(this, index, m_isWorkAvailable);
	}

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
		m_isWorkFinished = true;
	}
	if (!Queue.HasWork())
	{
		m_isWorkAvailable = false;
	}
}

void JobSystem::AddWork(std::function<void()> func, bool signalNewWork)
{
	GetJobQueue().Push(func);
	if (signalNewWork)
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
	while (!m_isWorkFinished)
	{
	}
}
