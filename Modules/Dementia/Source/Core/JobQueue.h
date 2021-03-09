#pragma once
#include <functional>
#include <mutex>
#include <vector>

class JobQueue
{
public:
	JobQueue();
	~JobQueue();

	bool HasWork() const;

	void Push(std::function<void()> InJob);

	std::function<void()> GetNextJob();

private:
	std::vector<std::function<void()>> Jobs;

	std::mutex Mutex;
};