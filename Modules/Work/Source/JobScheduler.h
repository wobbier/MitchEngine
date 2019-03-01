#pragma once
#include "Job.h"
#include <queue>
#include <atomic>
#include "Thread.h"
#include "Fiber.h"
#include <thread>
#include "Logger.h"
#include "Brofiler.h"
#include <mutex>
#include "boost/lockfree/queue.hpp"
#include "WaitFreeQueue.h"
#include "AtomicCounter.h"

constexpr static std::size_t kCacheLineSize = 64;

class JobScheduler
{
	enum class FiberDestination
	{
		None = 0,
		Pool,
		Waiting
	};

	enum class EmptyQueueBehavior
	{
		Spin = 0,
		Yield,
		Sleep
	};

	struct Work
	{
		Job Task;
		AtomicCounter* Counter = nullptr;
	};

	struct alignas(kCacheLineSize) ThreadData
	{
		ThreadData()
			: CurrentFiberIndex(kInvalidIndex)
			, PreviousFiberIndex(kInvalidIndex)
		{
		}
	public:
		Fiber CurrentFiber;

		std::size_t CurrentFiberIndex;
		std::size_t PreviousFiberIndex;
		FiberDestination OldFiberDestination { FiberDestination::None };
		WaitFreeQueue<Work> WorkQueue;
		std::size_t LastSuccessfulSteal = 1;
		std::atomic<bool>* OldFiberStoredFlag = nullptr;
		std::vector<std::pair<std::size_t, std::atomic<bool> *>> ReadyFibers;
		std::atomic_flag ReadFibersLock{};
		unsigned int FailedQueuePopAttempts = 0;

		std::mutex FailedQueuePopLock;
		std::condition_variable FailedQueuePopCV;
	};

	struct ThreadStartArgs
	{
		JobScheduler* Scheduler;
		unsigned int ThreadIndex;
	};

	class MainFiberStartArgs
	{
	public:
		JobScheduler* Scheduler = nullptr;
		JobFunc MainJob;
		void* Arg = nullptr;
	};

	static unsigned int __stdcall ThreadStart(void* const arg);

	constexpr static std::size_t kInvalidIndex = UINT_MAX;

public:
	JobScheduler()
	{
	}
	JobScheduler(JobScheduler const &) = delete;
	JobScheduler(JobScheduler &&) noexcept = delete;
	JobScheduler &operator=(JobScheduler const &) = delete;
	JobScheduler &operator=(JobScheduler &&) noexcept = delete;

	void AddSigleJob(Job const job, AtomicCounter* counter);

	void AddJobs(unsigned int const numTasks, Job const *const tasks, AtomicCounter *const counter);

	void Run(unsigned int const fiberPoolSize, JobFunc const mainJob, void* const mainJobArg = nullptr, unsigned int threadPoolSize = 0);

	static void MainFiberStart(boost::context::detail::transfer_t arg);

	static void FiberStart(boost::context::detail::transfer_t arg);

	std::size_t GetFreeFiberIndex() const;

	void CleanUpOldFiber();

	__declspec(noinline) std::size_t GetCurrentThreadIndex();


	const bool GetNextJob(Work* const task);

	void AddReadyFiber(std::size_t const pinnedThreadIndex, std::size_t fiberIndex, std::atomic<bool> *const fiberStoredFlag);

	void WaitForCounter(AtomicCounter *const counter, unsigned int const value, bool const pinToCurrentThread);



private:
	std::queue<Job> m_jobs;

	std::size_t m_numThreads = 0;
	std::vector<Thread> m_threads;
	unsigned int m_fiberPoolSize;
	std::atomic<bool> m_isInitialized = { false };
	std::atomic<bool> m_quit = { false };
	std::atomic_bool* m_freeFibers = nullptr;

	Fiber* m_fibers = nullptr;
	ThreadData* m_threadData = nullptr;
	std::atomic<EmptyQueueBehavior> m_emptyQueueBehavior{ EmptyQueueBehavior::Sleep };

};