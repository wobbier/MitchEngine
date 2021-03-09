#include "JobQueue.h"
#include <mutex>
#include <functional>

JobQueue::JobQueue()
{

}

JobQueue::~JobQueue()
{

}

bool JobQueue::HasWork() const
{
	return !Jobs.empty();
}

void JobQueue::AddJobBrad(std::function<void()> InJob)
{
	std::scoped_lock<std::mutex> lock(Mutex);
	Jobs.push_back(InJob);
}

std::function<void()> JobQueue::GetNextJob()
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

