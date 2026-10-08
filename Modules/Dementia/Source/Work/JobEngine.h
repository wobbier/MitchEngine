#pragma once
#include "Worker.h"
#include "StaticVector.h"
#include <random>

class JobEngine
{
public:
    JobEngine( std::size_t InNumThreads, std::size_t InJobsPerThread );
    ~JobEngine();

    Worker* GetRandomWorker();
    Worker* GetThreadWorker();

    void ClearWorkerPools();

private:
    StaticVector<Worker> Workers;
    std::mt19937 RandomEngine { std::random_device()() };

    Worker* FindThreadWorker( const std::thread::id InThreadId );
};