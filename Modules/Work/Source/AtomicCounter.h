
#pragma once

#include <atomic>
#include <limits>
#include <vector>

class AtomicCounter
{
#ifndef NUM_WAITING_FIBER_SLOTS
#	define NUM_WAITING_FIBER_SLOTS 4
#endif

public:
	explicit AtomicCounter(class JobScheduler *taskScheduler, unsigned int initialValue = 0, unsigned int fiberSlots = NUM_WAITING_FIBER_SLOTS);

	AtomicCounter(AtomicCounter const &) = delete;
	AtomicCounter(AtomicCounter &&) noexcept = delete;
	AtomicCounter &operator=(AtomicCounter const &) = delete;
	AtomicCounter &operator=(AtomicCounter &&) noexcept = delete;
	~AtomicCounter();

private:
	class JobScheduler *m_taskScheduler;
	std::atomic_uint m_value;
	std::atomic_uint m_lock;

	std::atomic<bool> *m_freeSlots;

	struct WaitingFiberBundle
	{
		WaitingFiberBundle();

		std::atomic<bool> InUse;
		std::size_t FiberIndex{ 0 };
		unsigned int TargetValue{ 0 };
		std::atomic<bool> *FiberStoredFlag{ nullptr };
		std::size_t PinnedThreadIndex;
	};
	std::vector<WaitingFiberBundle> m_waitingFibers;

	friend class JobScheduler;

public:
	unsigned int Load(std::memory_order const memoryOrder = std::memory_order_seq_cst) const noexcept
	{
		return m_value.load(memoryOrder);
	}
	
	void Store(unsigned int const x, std::memory_order const memoryOrder = std::memory_order_seq_cst)
	{
		// Enter shared section
		m_lock.fetch_add(1U, std::memory_order_seq_cst);
		m_value.store(x, memoryOrder);
		CheckWaitingFibers(x);
	}
	
	unsigned int FetchAdd(unsigned int const x, std::memory_order const memoryOrder = std::memory_order_seq_cst)
	{
		// Enter shared section
		m_lock.fetch_add(1U, std::memory_order_seq_cst);
		const unsigned int prev = m_value.fetch_add(x, memoryOrder);
		CheckWaitingFibers(prev + x);

		return prev;
	}
	
	unsigned int FetchSub(unsigned int const x, std::memory_order const memoryOrder = std::memory_order_seq_cst)
	{
		// Enter shared section
		m_lock.fetch_add(1U, std::memory_order_seq_cst);
		const unsigned int prev = m_value.fetch_sub(x, memoryOrder);
		CheckWaitingFibers(prev - x);

		return prev;
	}

private:
	
	bool AddFiberToWaitingList(std::size_t fiberIndex, unsigned int targetValue, std::atomic<bool> *fiberStoredFlag,
		std::size_t pinnedThreadIndex = std::numeric_limits<std::size_t>::max());

	void CheckWaitingFibers(unsigned int value);
};

