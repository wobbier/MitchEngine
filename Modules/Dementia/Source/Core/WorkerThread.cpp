#include "WorkerThread.h"
#include "JobSystem.h"

void WorkerThread::SetJobSystem(JobSystem* InJobSystem, std::size_t InIndex, HANDLE EventHandle)
{
	Owner = InJobSystem;
	Index = InIndex;
	WorkAvailableHandle = EventHandle;
}

void WorkerThread::Start()
{
	Create([this]() {
	while (IsAlive())
	{
		if (IsPaused)
		{
			IsPaused = false;
			Owner->WorkerThreadActive(Index);
		}

		auto& queue = Owner->GetJobQueue();
		auto job = queue.GetNextJob();
		if (job)
		{
			job();
		}
		else
		{
			Owner->WorkerThreadSleeping(Index);
			Pause();
		}
	}
	}, "Job Thread");
}

void WorkerThread::Pause()
{
	IsPaused = true;

	WaitForSingleObject(WorkAvailableHandle, INFINITE);
}
