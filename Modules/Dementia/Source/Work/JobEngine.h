#pragma once
#include "Worker.h"
#include "StaticVector.h"

class JobEngine
{
public:
	JobEngine(std::size_t InNumThreads, std::size_t InJobsPerThread);
	~JobEngine();

	Worker* GetRandomWorker();
	Worker* GetThreadWorker();

	void ClearWorkerPools();

private:
	StaticVector<Worker> Workers;

	Worker* FindThreadWorker(const std::thread::id InThreadId);
};