#include "JobScheduler.h"



unsigned int __stdcall JobScheduler::ThreadStart(void* const arg)
{
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

void JobScheduler::AddSigleJob(Job const job, AtomicCounter* counter)
{
	if (counter != nullptr)
	{
		counter->Store(1);
	}
	const Work work = { job, counter };
	m_threadData[GetCurrentThreadIndex()].WorkQueue.Push(work);

	const EmptyQueueBehavior behavior = m_emptyQueueBehavior.load(std::memory_order_relaxed);
	if (behavior == EmptyQueueBehavior::Sleep)
	{
		for (std::size_t i = 0; i < m_numThreads; ++i)
		{
			std::unique_lock<std::mutex> lock(m_threadData[i].FailedQueuePopLock);
			if (m_threadData[i].FailedQueuePopAttempts >= 5)
			{
				m_threadData[i].FailedQueuePopAttempts = 0;
				m_threadData[i].FailedQueuePopCV.notify_one();

				break;
			}
		}
	}
}

void JobScheduler::AddJobs(unsigned int const numTasks, Job const *const tasks, AtomicCounter *const counter)
{
	if (counter != nullptr)
	{
		counter->Store(numTasks);
	}

	ThreadData &tls = m_threadData[GetCurrentThreadIndex()];
	for (unsigned int i = 0; i < numTasks; ++i)
	{
		const Work bundle = { tasks[i], counter };
		tls.WorkQueue.Push(bundle);
	}

	const EmptyQueueBehavior behavior = m_emptyQueueBehavior.load(std::memory_order_relaxed);
	if (behavior == EmptyQueueBehavior::Sleep)
	{
		// Wake all the threads
		for (std::size_t i = 0; i < m_numThreads; ++i)
		{
			{
				std::unique_lock<std::mutex> lock(m_threadData[i].FailedQueuePopLock);
				m_threadData[i].FailedQueuePopAttempts = 0;
			}
			m_threadData[i].FailedQueuePopCV.notify_all();
		}
	}
}

void JobScheduler::Run(unsigned int const fiberPoolSize, JobFunc const mainJob, void* const mainJobArg /*= nullptr*/, unsigned int threadPoolSize /*= 0*/)
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

	for (std::size_t i = 1; i < m_numThreads; ++i)
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
	m_threadData[1].CurrentFiberIndex = freeFiberIndex;
	m_threadData[1].CurrentFiber.SwitchToFiber(freeFiber);

	for (std::size_t i = 1; i < m_numThreads; ++i)
	{
		WaitForSingleObject(m_threads[i].Handle, INFINITE);
	}
}

void JobScheduler::MainFiberStart(boost::context::detail::transfer_t arg)
{
	MainFiberStartArgs* args = reinterpret_cast<MainFiberStartArgs*>(arg.data);
	JobScheduler* jobScheduler = args->Scheduler;

	args->MainJob(jobScheduler, args->Arg);

	jobScheduler->m_quit.store(true, std::memory_order_release);

	if (jobScheduler->m_emptyQueueBehavior.load(std::memory_order_relaxed) == EmptyQueueBehavior::Sleep)
	{
		for (std::size_t i = 0; i < jobScheduler->m_numThreads; ++i)
		{
			{
				std::unique_lock<std::mutex> lock(jobScheduler->m_threadData[i].FailedQueuePopLock);
				jobScheduler->m_threadData[i].FailedQueuePopAttempts = 0;
			}
			jobScheduler->m_threadData[i].FailedQueuePopCV.notify_all();
		}
	}

	ThreadData& data = jobScheduler->m_threadData[jobScheduler->GetCurrentThreadIndex()];
	jobScheduler->m_fibers[data.CurrentFiberIndex].SwitchToFiber(&data.CurrentFiber);
}

void JobScheduler::FiberStart(boost::context::detail::transfer_t arg)
{
	JobScheduler* const jobScheduler = reinterpret_cast<JobScheduler*>(arg.data);

	jobScheduler->CleanUpOldFiber();

	while (!jobScheduler->m_quit.load(std::memory_order_acquire))
	{
		std::size_t waitingFiberIndex = kInvalidIndex;
		ThreadData& data = jobScheduler->m_threadData[jobScheduler->GetCurrentThreadIndex()];

		while (data.ReadFibersLock.test_and_set(std::memory_order_acquire))
		{
			// Spin
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

		if (waitingFiberIndex != kInvalidIndex)
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
			Work task{};
			bool const success = jobScheduler->GetNextJob(&task);
			EmptyQueueBehavior const behavior = jobScheduler->m_emptyQueueBehavior.load(std::memory_order::memory_order_relaxed);
			if (success)
			{
				if (behavior == EmptyQueueBehavior::Sleep)
				{
					std::unique_lock<std::mutex> lock(data.FailedQueuePopLock);
					data.FailedQueuePopAttempts = 0;
				}

				task.Task.Func(jobScheduler, task.Task.Data);
				if (task.Counter)
				{
					task.Counter->FetchSub(1);
				}
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
					break;
				}
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

std::size_t JobScheduler::GetFreeFiberIndex() const
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

void JobScheduler::CleanUpOldFiber()
{
	ThreadData& data = m_threadData[GetCurrentThreadIndex()];
	switch (data.OldFiberDestination)
	{
	case FiberDestination::Pool:
		m_freeFibers[data.PreviousFiberIndex].store(true, std::memory_order_release);
		data.OldFiberDestination = FiberDestination::None;
		data.PreviousFiberIndex = kInvalidIndex;
		break;
	case FiberDestination::Waiting:
		data.OldFiberStoredFlag->store(true, std::memory_order_relaxed);
		data.OldFiberDestination = FiberDestination::None;
		data.PreviousFiberIndex = kInvalidIndex;
		break;
	case FiberDestination::None:
	default:
		break;
	}
}

std::size_t JobScheduler::GetCurrentThreadIndex()
{
	DWORD const threadId = GetCurrentThreadId();
	for (std::size_t i = 0; i < m_numThreads; ++i)
	{
		if (m_threads[i].Id == threadId)
		{
			return i;
		}
	}

	return kInvalidIndex;
}

const bool JobScheduler::GetNextJob(Work* const task)
{
	size_t const currentThreadIndex = GetCurrentThreadIndex();
	ThreadData& data = m_threadData[currentThreadIndex];

	if (data.WorkQueue.Pop(task))
	{
		return true;
	}

	const std::size_t busyThreadIndex = data.LastSuccessfulSteal;
	for (std::size_t i = 0; i < m_numThreads; ++i)
	{
		const std::size_t threadToSealFrom = (busyThreadIndex + i) % m_numThreads;
		if (threadToSealFrom == currentThreadIndex)
		{
			continue;
		}

		ThreadData& otherData = m_threadData[threadToSealFrom];
		if (otherData.WorkQueue.Steal(task))
		{
			data.LastSuccessfulSteal = threadToSealFrom;
			return true;
		}
	}
	return false;
}

void JobScheduler::AddReadyFiber(std::size_t const pinnedThreadIndex, std::size_t fiberIndex, std::atomic<bool> *const fiberStoredFlag)
{
	ThreadData* tls;
	if (pinnedThreadIndex == std::numeric_limits<std::size_t>::max())
	{
		tls = &m_threadData[GetCurrentThreadIndex()];
	}
	else
	{
		tls = &m_threadData[pinnedThreadIndex];
	}

	// Lock
	while (tls->ReadFibersLock.test_and_set(std::memory_order_acquire))
	{
		// Spin
	}

	tls->ReadyFibers.emplace_back(fiberIndex, fiberStoredFlag);

	// Unlock
	tls->ReadFibersLock.clear(std::memory_order_release);

	// If the Task is pinned, we add the Task to the pinned thread's ReadyFibers queue, instead
	// of our own. Normally, this works fine; the other thread will pick it up next time it
	// searches for a Task to run.
	//
	// However, if we're using EmptyQueueBehavior::Sleep, the other thread could be sleeping
	// Therefore, we need to kick the thread to make sure that it's awake
	const EmptyQueueBehavior behavior = m_emptyQueueBehavior.load(std::memory_order::memory_order_relaxed);
	if (behavior == EmptyQueueBehavior::Sleep)
	{
		// Kick the thread
		{
			std::unique_lock<std::mutex> lock(tls->FailedQueuePopLock);
			tls->FailedQueuePopAttempts = 0;
		}
		tls->FailedQueuePopCV.notify_all();
	}
}

void JobScheduler::WaitForCounter(AtomicCounter *const counter, unsigned int const value, bool const pinToCurrentThread)
{
	// Fast out
	if (counter->Load(std::memory_order_relaxed) == value)
	{
		return;
	}

	ThreadData &tls = m_threadData[GetCurrentThreadIndex()];
	std::size_t const currentFiberIndex = tls.CurrentFiberIndex;

	std::size_t pinnedThreadIndex;
	if (pinToCurrentThread)
	{
		pinnedThreadIndex = GetCurrentThreadIndex();
	}
	else
	{
		pinnedThreadIndex = std::numeric_limits<std::size_t>::max();
	}
	auto *const fiberStoredFlag = new std::atomic<bool>(false);
	bool const alreadyDone = counter->AddFiberToWaitingList(tls.CurrentFiberIndex, value, fiberStoredFlag, pinnedThreadIndex);

	// The counter finished while we were trying to put it in the waiting list
	// Just clean up and trivially return
	if (alreadyDone)
	{
		delete fiberStoredFlag;
		return;
	}

	// Get a free fiber
	std::size_t const freeFiberIndex = GetFreeFiberIndex();

	// Fill in tls
	tls.PreviousFiberIndex = currentFiberIndex;
	tls.CurrentFiberIndex = freeFiberIndex;
	tls.OldFiberDestination = FiberDestination::Waiting;
	tls.OldFiberStoredFlag = fiberStoredFlag;

	m_fibers[currentFiberIndex].SwitchToFiber(&m_fibers[freeFiberIndex]);
	// Switch

	// And we're back
	CleanUpOldFiber();
}
