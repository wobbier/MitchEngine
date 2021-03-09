#pragma once
#include "Thread.h"
#include <wtypes.h>

class JobSystem;

class WorkerThread
	: public Thread
{
public:
	WorkerThread() = default;

	void SetJobSystem(JobSystem* InJobSystem, std::size_t InIndex, HANDLE EventHandle);

	void Start();

	void Pause();

private:
	JobSystem* Owner = nullptr;
	std::size_t Index = 0;
	HANDLE WorkAvailableHandle;

	bool IsPaused = false;
};