#include "WorkerThread.h"
#include "JobSystem.h"
#include <optick.h>

void WorkerThread::SetJobSystem(JobSystem* InJobSystem, std::size_t InIndex, const std::atomic_bool& EventHandle)
{
	Owner = InJobSystem;
	Index = InIndex;
	WorkAvailableHandle = &EventHandle;
}

void WorkerThread::Start()
{
	Create([this]() {
		OPTICK_THREAD(ThreadName.c_str());
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
	OPTICK_CATEGORY("Pause", Optick::Category::Wait);
	IsPaused = true;

	while (!(*WorkAvailableHandle))
	{
	}
}
