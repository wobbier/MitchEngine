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

using AtomicCounter = std::atomic<size_t>;

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

	struct alignas(kCacheLineSize) ThreadData
	{
		ThreadData() = default;

		Fiber CurrentFiber;

		std::size_t CurrentFiberIndex = UINT_MAX;
		std::size_t PreviousFiberIndex = UINT_MAX;
		std::atomic_flag ReadFibersLock;
		FiberDestination OldFiberDestination = FiberDestination::None;
		std::atomic<bool>* OldFiberStoredFlag = nullptr;
		std::vector<std::pair<std::size_t, std::atomic<bool> *>> ReadyFibers;

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

	static unsigned int __stdcall ThreadStart(void* const arg)
	{
		BROFILER_THREAD("Thread");
		ThreadStartArgs* threadArgs = reinterpret_cast<ThreadStartArgs*>(arg);
		JobScheduler* jobScheduler = threadArgs->Scheduler;
		unsigned int const index = threadArgs->ThreadIndex;

		delete threadArgs;

		while (!jobScheduler->m_isInitialized.load(std::memory_order_acquire))
		{
		}

		std::size_t const freeFiberIndex = jobScheduler->GetFreeFiberIndex();

		jobScheduler->m_threadData[index].CurrentFiberIndex = freeFiberIndex;
		jobScheduler->m_threadData[index].CurrentFiber.SwitchToFiber(&jobScheduler->m_fibers[freeFiberIndex]);

		_endthreadex(0);

		return 0;
	}

public:
	JobScheduler()
	{
	}
	JobScheduler(JobScheduler const &) = delete;
	JobScheduler(JobScheduler &&) noexcept = delete;
	JobScheduler &operator=(JobScheduler const &) = delete;
	JobScheduler &operator=(JobScheduler &&) noexcept = delete;


	void AddJob(Job const job, AtomicCounter* counter)
	{
		m_jobs.push(job);
	}

	void Run(unsigned int const fiberPoolSize, JobFunc const mainJob, void* const mainJobArg = nullptr, unsigned int threadPoolSize = 0)
	{
		m_isInitialized.store(false, std::memory_order::memory_order_release);
		m_quit.store(false, std::memory_order_release);
		m_fiberPoolSize = fiberPoolSize;
		m_fibers = new Fiber[fiberPoolSize];
		m_freeFibers = new std::atomic_bool[fiberPoolSize];

		for (unsigned int i = 0; i < fiberPoolSize; ++i)
		{
			m_fibers[i] = Fiber(512000, FiberStart, this);
			m_freeFibers[i].store(true, std::memory_order_release);
		}

		if (threadPoolSize == 0)
		{
			m_numThreads = std::thread::hardware_concurrency();
		}
		else
		{
			m_numThreads = threadPoolSize;
		}

		m_threads.resize(m_numThreads);

		m_threadData = new ThreadData[m_numThreads];

		for (std::size_t i = 0; i < m_numThreads; ++i)
		{
			m_threadData[i].ReadFibersLock.clear();
		}

		SetThreadAffinityMask(::GetCurrentThread(), 1ULL << 0);

		m_threads[0] = GetCurrentThreadInfo();
		m_threads[0].Handle = INVALID_HANDLE_VALUE;

		for (std::size_t i = 0; i < m_numThreads; ++i)
		{
			ThreadStartArgs* const args = new ThreadStartArgs();
			args->Scheduler = this;
			args->ThreadIndex = static_cast<unsigned int>(i);

			const bool created = CreateThread(524288, ThreadStart, args, i, &m_threads[i]);
			if (!created)
			{
				return;
			}
		}

		m_isInitialized.store(true, std::memory_order_release);

		std::size_t const freeFiberIndex = GetFreeFiberIndex();
		Fiber* freeFiber = &m_fibers[freeFiberIndex];

		MainFiberStartArgs args = { 0 };
		args.Scheduler = this;
		args.MainJob = mainJob;
		args.Arg = mainJobArg;

		freeFiber->Reset(MainFiberStart, &args);
		m_threadData[0].CurrentFiberIndex = freeFiberIndex;
		m_threadData[0].CurrentFiber.SwitchToFiber(freeFiber);

		for (std::size_t i = 1; i < m_numThreads; ++i)
		{
			WaitForSingleObject(m_threads[i].Handle, INFINITE);
		}
	}

	static void MainFiberStart(boost::context::detail::transfer_t arg)
	{
		MainFiberStartArgs* args = reinterpret_cast<MainFiberStartArgs*>(arg.data);
		JobScheduler* jobScheduler = args->Scheduler;

		args->MainJob(jobScheduler, args->Arg);

		jobScheduler->m_quit.store(true, std::memory_order_release);
	}

	static void FiberStart(boost::context::detail::transfer_t arg)
	{
		JobScheduler* const jobScheduler = reinterpret_cast<JobScheduler*>(arg.data);

		jobScheduler->CleanUpOldFiber();

		while (!jobScheduler->m_quit.load(std::memory_order_acquire))
		{
			std::size_t waitingFiberIndex = INT_MAX;
			ThreadData& data = jobScheduler->m_threadData[jobScheduler->GetCurrentThreadIndex()];

			while (data.ReadFibersLock.test_and_set(std::memory_order_acquire))
			{
			}

			for (auto it = data.ReadyFibers.begin(); it != data.ReadyFibers.end(); ++it)
			{
				if (!it->second->load(std::memory_order_relaxed))
				{
					continue;
				}

				waitingFiberIndex = it->first;
				delete it->second;
				data.ReadyFibers.erase(it);
				break;
			}

			data.ReadFibersLock.clear(std::memory_order_release);

			if (waitingFiberIndex != INT_MAX)
			{
				data.PreviousFiberIndex = data.CurrentFiberIndex;
				data.CurrentFiberIndex = waitingFiberIndex;
				data.OldFiberDestination = FiberDestination::Pool;

				jobScheduler->m_fibers[data.PreviousFiberIndex].SwitchToFiber(&jobScheduler->m_fibers[data.CurrentFiberIndex]);

				jobScheduler->CleanUpOldFiber();
				if (jobScheduler->m_emptyQueueBehavior.load(std::memory_order_relaxed) == EmptyQueueBehavior::Sleep)
				{
					std::unique_lock<std::mutex> lock(data.FailedQueuePopLock);
					data.FailedQueuePopAttempts = 0;
				}
			}
			else
			{
				EmptyQueueBehavior const behavior = jobScheduler->m_emptyQueueBehavior.load(std::memory_order::memory_order_relaxed);
				if (false)
				{

				}
				else
				{
					switch (behavior)
					{
					case EmptyQueueBehavior::Yield:
						std::this_thread::yield();
						break;
					case EmptyQueueBehavior::Sleep:
					{
						std::unique_lock<std::mutex> lock(data.FailedQueuePopLock);

						while (data.ReadFibersLock.test_and_set(std::memory_order_acquire))
						{
						}
						if (data.ReadyFibers.empty())
						{
							++data.FailedQueuePopAttempts;
						}

						data.ReadFibersLock.clear(std::memory_order_release);

						while (data.FailedQueuePopAttempts >= 5)
						{
							data.FailedQueuePopCV.wait(lock);
						}
					}
					break;
					case EmptyQueueBehavior::Spin:
					default:
						break;
					}
				}
			}
		}

		ThreadData& data = jobScheduler->m_threadData[jobScheduler->GetCurrentThreadIndex()];
		jobScheduler->m_fibers[data.CurrentFiberIndex].SwitchToFiber(&data.CurrentFiber);
	}

	std::size_t const GetFreeFiberIndex()
	{
		for (unsigned int i = 0;; ++i)
		{
			for (std::size_t j = 0; j < m_fiberPoolSize; ++j)
			{
				if (!m_freeFibers[j].load(std::memory_order_relaxed))
				{
					continue;
				}

				if (!m_freeFibers[j].load(std::memory_order_acquire))
				{
					continue;
				}

				bool expected = true;
				if (std::atomic_compare_exchange_weak_explicit(&m_freeFibers[i], &expected, false, std::memory_order_release, std::memory_order_relaxed))
				{
					return j;
				}
			}

			if (i > 10)
			{
				Logger::GetInstance().Log(Logger::LogType::Warning, "No free fibers left in the pool");
			}
		}
	}

	void CleanUpOldFiber()
	{
		ThreadData& data = m_threadData[GetCurrentThreadIndex()];
		switch (data.OldFiberDestination)
		{
		case FiberDestination::Pool:
			m_freeFibers[data.PreviousFiberIndex].store(true, std::memory_order_release);
			data.OldFiberDestination = FiberDestination::None;
			data.PreviousFiberIndex = INT_MAX;
			break;
		case FiberDestination::Waiting:
			data.OldFiberStoredFlag->store(true, std::memory_order_relaxed);
			data.OldFiberDestination = FiberDestination::None;
			data.PreviousFiberIndex = INT_MAX;
			break;
		case FiberDestination::None:
		default:
			break;
		}
	}

	__declspec(noinline) std::size_t GetCurrentThreadIndex()
	{
		DWORD const threadId = GetCurrentThreadId();
		for (std::size_t i = 0; i < m_numThreads; ++i)
		{
			if (m_threads[i].Id == threadId)
			{
				return i;
			}
		}

		return INT_MAX;
	}

private:
	std::queue<Job> m_jobs;

	std::size_t m_numThreads = 0;
	std::vector<Thread> m_threads;
	unsigned int m_fiberPoolSize;
	std::atomic<bool> m_isInitialized = 0;
	std::atomic<bool> m_quit = 0;

	Fiber* m_fibers = nullptr;
	std::atomic_bool* m_freeFibers = nullptr;
	ThreadData* m_threadData = nullptr;
	std::atomic<EmptyQueueBehavior> m_emptyQueueBehavior{ EmptyQueueBehavior::Sleep };

};