#pragma once
#include "Pool.h"
#include <atomic>
#include "JobQueue.h"
#include <thread>

class Job;

class Worker
{
public:
	enum class Mode : uint8_t
	{
		Background = 0,
		Foreground
	};

	enum class State : uint8_t
	{
		Idle = 0,
		Running,
		Stopping
	};

	Worker(class JobEngine* engine, std::size_t InMaxJobs, Mode InMode = Mode::Background);
	~Worker();

	void Start();
	void Stop();
	bool IsRunning() const;
	Pool& GetPool();
	void Submit(Job* InJob);
	void Wait(Job* InJob);

	std::thread::id GetThreadId() const;

private:
	Pool WorkPool;
	class JobEngine* jobEngine;
	JobQueue queue;
	std::thread::id ThreadId;
	std::thread WorkerThread;
	std::atomic<State> ThreadState;
	std::atomic<Mode> ThreadMode;

	Job* GetJob();
	void Join();
};