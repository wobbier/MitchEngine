#include "JobQueue.h"
#include <algorithm>

JobQueue::JobQueue(std::size_t InMaxJobs)
	: Jobs{ InMaxJobs }
	, Bottom(0)
	, Top(0)
{

}

bool JobQueue::Push(Job* InJob)
{
	int bottom = Bottom.load(std::memory_order_seq_cst);

	if (bottom < static_cast<int>(Jobs.size()))
	{
		Jobs[bottom] = InJob;
		Bottom.store(bottom + 1, std::memory_order_seq_cst);

		return true;
	}
	else
	{
		return false;
	}
}

Job* JobQueue::Pop()
{
	int bottom = Bottom.load(std::memory_order_seq_cst);
	bottom = std::max(0, bottom - 1);
	Bottom.store(bottom, std::memory_order_seq_cst);

	int top = Top.load(std::memory_order_seq_cst);

	if (top <= bottom)
	{
		Job* job = Jobs[bottom];
		if (top != bottom)
		{
			return job;
		}
		else
		{
			int stolenTop = top + 1;
			if (Top.compare_exchange_strong(stolenTop, top + 1, std::memory_order_seq_cst))
			{
				// Job was stolen already
				Bottom.store(stolenTop, std::memory_order_release);
				return nullptr;
			}

			return job;
		}
	}
	else
	{
		Bottom.store(top, std::memory_order_seq_cst);
		return nullptr;
	}
}

Job* JobQueue::Steal()
{
	int top = Top.load(std::memory_order_seq_cst);
	int bottom = Bottom.load(std::memory_order_seq_cst);
	if (Top < Bottom)
	{
		Job* job = Jobs[top];

		if (Top.compare_exchange_strong(top, top + 1, std::memory_order_seq_cst))
		{
			return job;
		}

		return nullptr;
	}
	else
	{
		return nullptr;
	}
}

void JobQueue::Clear()
{
	Top = 0;
	Bottom = 0;
}
