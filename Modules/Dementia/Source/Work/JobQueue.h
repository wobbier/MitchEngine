#pragma once
#include <vector>
#include <atomic>

class Job;

class JobQueue
{
public:
	JobQueue(std::size_t InMaxJobs);

	bool Push(Job* InJob);

	Job* Pop();
	Job* Steal();
	std::size_t Size() const;
	bool Empty() const;
	void Clear();

private:
	std::vector<Job*> Jobs;
	std::atomic_size_t Top;
	std::atomic_size_t Bottom;
};