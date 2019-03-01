
#include "JobScheduler.h"
#include "AtomicCounter.h"
#include <atomic>
#include <vector>

AtomicCounter::AtomicCounter(JobScheduler *const taskScheduler, unsigned int initialValue, unsigned int fiberSlots)
	: m_taskScheduler(taskScheduler), m_value(initialValue), m_lock(0), m_waitingFibers(fiberSlots)
{
	m_freeSlots = new std::atomic<bool>[fiberSlots];

	for (unsigned int i = 0; i < fiberSlots; ++i)
	{
		m_freeSlots[i].store(true);
		m_waitingFibers[i].InUse.store(true);
	}
}

AtomicCounter::~AtomicCounter()
{
	delete[] m_freeSlots;
}

AtomicCounter::WaitingFiberBundle::WaitingFiberBundle() : InUse(true), PinnedThreadIndex(std::numeric_limits<std::size_t>::max())
{
}

bool AtomicCounter::AddFiberToWaitingList(std::size_t const fiberIndex, unsigned int const targetValue, std::atomic<bool> *const fiberStoredFlag,
	std::size_t const pinnedThreadIndex)
{
	for (std::size_t i = 0; i < m_waitingFibers.size(); ++i)
	{
		bool expected = true;
		// Try to acquire the slot
		if (!std::atomic_compare_exchange_strong_explicit(&m_freeSlots[i], &expected, false, std::memory_order_seq_cst,
			std::memory_order_relaxed))
		{
			// Failed the race or the slot was already full
			continue;
		}

		// We found a free slot
		m_waitingFibers[i].FiberIndex = fiberIndex;
		m_waitingFibers[i].TargetValue = targetValue;
		m_waitingFibers[i].FiberStoredFlag = fiberStoredFlag;
		m_waitingFibers[i].PinnedThreadIndex = pinnedThreadIndex;
		// We have to use memory_order_seq_cst here instead of memory_order_acquire to prevent
		// later loads from being re-ordered before this store
		m_waitingFibers[i].InUse.store(false, std::memory_order_seq_cst);

		// Events are now being tracked

		// Now we do a check of the waiting fiber, to see if we reached the target value while we were storing
		// everything
		unsigned int const value = m_value.load(std::memory_order_relaxed);
		if (m_waitingFibers[i].InUse.load(std::memory_order_acquire))
		{
			return false;
		}

		if (m_waitingFibers[i].TargetValue == value)
		{
			expected = false;
			// Try to acquire InUse
			if (!std::atomic_compare_exchange_strong_explicit(&m_waitingFibers[i].InUse, &expected, true, std::memory_order_seq_cst,
				std::memory_order_relaxed))
			{
				// Failed the race. Another thread got to it first.
				return false;
			}
			// Signal that the slot is now free
			// Leave IneUse == true
			m_freeSlots[i].store(true, std::memory_order_release);

			return true;
		}

		return false;
	}

	return false;
}

void AtomicCounter::CheckWaitingFibers(unsigned int const value)
{
	std::vector<std::size_t> readyFiberIndices(m_waitingFibers.size(), 0);
	unsigned int nextIndex = 0;

	for (std::size_t i = 0; i < m_waitingFibers.size(); ++i)
	{
		// Check if the slot is full
		if (m_freeSlots[i].load(std::memory_order_acquire))
		{
			continue;
		}
		// Check if the slot is being modified by another thread
		if (m_waitingFibers[i].InUse.load(std::memory_order_acquire))
		{
			continue;
		}

		// Do the actual value check
		if (m_waitingFibers[i].TargetValue == value)
		{
			bool expected = false;
			// Try to acquire InUse
			if (!std::atomic_compare_exchange_strong_explicit(&m_waitingFibers[i].InUse, &expected, true, std::memory_order_seq_cst,
				std::memory_order_relaxed))
			{
				// Failed the race. Another thread got to it first
				continue;
			}
			readyFiberIndices[nextIndex++] = i;
		}
	}
	// Exit shared section
	m_lock.fetch_sub(1U, std::memory_order_seq_cst);
	// Wait for all threads to exit the shared section if there are fibers to ready
	if (nextIndex > 0)
	{
		while (m_lock.load() > 0)
		{
			// Spin
		}

		for (unsigned int i = 0; i < nextIndex; ++i)
		{
			// Add the fiber to the TaskScheduler's ready list
			const std::size_t index = readyFiberIndices[i];
			m_taskScheduler->AddReadyFiber(m_waitingFibers[index].PinnedThreadIndex, m_waitingFibers[index].FiberIndex,
				m_waitingFibers[index].FiberStoredFlag);
			// Signal that the slot is free
			// Leave InUse == true
			m_freeSlots[i].store(true, std::memory_order_release);
		}
	}
}