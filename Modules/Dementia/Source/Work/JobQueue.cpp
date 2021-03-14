#include "JobQueue.h"
#include <algorithm>
#include "Job.h"

JobQueue::JobQueue(std::size_t InMaxJobs)
	: Jobs{ InMaxJobs }
	, Bottom(0)
	, Top(0)
{

}

bool JobQueue::Push(Job* InJob)
{
	std::size_t bottom = Bottom.load(std::memory_order_acquire);

	if (bottom < Jobs.size())
	{
		Jobs[bottom % Jobs.size()] = InJob;

		std::atomic_thread_fence(std::memory_order_release);

		Bottom.store(bottom + 1, std::memory_order_release);

		return true;
	}
	else
	{
		return false;
	}
}

Job* JobQueue::Pop()
{
	std::size_t bottom = Bottom.load(std::memory_order_acquire);
	if (bottom > 0)
	{
		bottom = bottom - 1;
		Bottom.store(bottom, std::memory_order_release);
	}

	std::atomic_thread_fence(std::memory_order_release);

	std::size_t top = Top.load(std::memory_order_acquire);

	if (top <= bottom)
	{
		Job* job = Jobs[bottom % Jobs.size()];
		if (top == bottom)
		{
			std::size_t expectedTop = top;
			const std::size_t nextTop = top + 1;
			std::size_t desiredTop = nextTop;
			if (!Top.compare_exchange_strong(expectedTop, desiredTop, std::memory_order_acq_rel))
			{
				// Job was stolen already
				return nullptr;
			}

			Bottom.store(nextTop, std::memory_order_release);
		}
		return job;
	}
	else
	{
		Bottom.store(top, std::memory_order_release);
		return nullptr;
	}
}

Job* JobQueue::Steal()
{
	std::size_t top = Top.load(std::memory_order_acquire);

	std::atomic_thread_fence(std::memory_order_acquire);
	
	std::size_t bottom = Bottom.load(std::memory_order_acquire);
	if (top < bottom)
	{
		Job* job = Jobs[top % Jobs.size()];
		const std::size_t nextTop = top + 1;
		std::size_t desiredTop = nextTop;

		if (!Top.compare_exchange_weak(top, desiredTop, std::memory_order_acq_rel))
		{
			return nullptr;
		}

		return job;
	}
	else
	{
		return nullptr;
	}
}

void JobQueue::Clear()
{
	Bottom = 0;
	Top = 0;
}
