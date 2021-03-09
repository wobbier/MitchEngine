#pragma once
#include "Thread.h"
#include <atomic>

class JobSystem;

class WorkerThread
	: public Thread
{
public:
	WorkerThread() = default;

	void SetJobSystem(JobSystem* InJobSystem, std::size_t InIndex, const std::atomic_bool& EventHandle);

	void Start();

	void Pause();

private:
	JobSystem* Owner = nullptr;
	std::size_t Index = 0;
	const std::atomic_bool* WorkAvailableHandle;

	bool IsPaused = false;
};