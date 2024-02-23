#pragma once
#include <functional>
#include <mutex>
#include <vector>

class JobQueueOld
{
public:
    JobQueueOld();
    ~JobQueueOld();

    bool HasWork() const;

    void Push( std::function<void()> InJob );

    std::function<void()> GetNextJob();

private:
    std::vector<std::function<void()>> Jobs;

    std::mutex Mutex;
};