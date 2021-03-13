#include "JobQueueOld.h"
#include <mutex>
#include <functional>

JobQueueOld::JobQueueOld()
{

}

JobQueueOld::~JobQueueOld()
{

}

bool JobQueueOld::HasWork() const
{
	return !Jobs.empty();
}

void JobQueueOld::Push(std::function<void()> InJob)
{
	std::scoped_lock<std::mutex> lock(Mutex);
	Jobs.push_back(InJob);
}

std::function<void()> JobQueueOld::GetNextJob()
{
	std::scoped_lock<std::mutex> lock(Mutex);

	if (!Jobs.empty())
	{
		auto job = *Jobs.rbegin();
		Jobs.pop_back();
		return job;
	}
	return nullptr;
}

