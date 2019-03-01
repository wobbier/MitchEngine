#pragma once

#include <atomic>
#include <memory>
#include <vector>

template <typename T>
class WaitFreeQueue
{
private:
	constexpr static std::size_t kStartingCircularArraySize = 32;

public:
	WaitFreeQueue()
		: m_top(1),    // m_top and m_bottom must start at 1
		m_bottom(1), // Otherwise, the first Pop on an empty queue will underflow m_bottom
		m_array(new CircularArray(kStartingCircularArraySize))
	{
	}

	WaitFreeQueue(WaitFreeQueue const &) = delete;
	WaitFreeQueue(WaitFreeQueue &&) noexcept = delete;
	WaitFreeQueue &operator=(WaitFreeQueue const &) = delete;
	WaitFreeQueue &operator=(WaitFreeQueue &&) noexcept = delete;
	~WaitFreeQueue()
	{
		delete m_array.load(std::memory_order_relaxed);
	}

private:
	class CircularArray
	{
	public:
		explicit CircularArray(std::size_t const n) : m_items(n)
		{
			//Logger::GetInstance().LogAssert("n must be a power of 2", !(n == 0) && !(n & (n - 1)));
		}

	private:
		std::vector<T> m_items;
		std::unique_ptr<CircularArray> m_previous;

	public:
		std::size_t Size() const
		{
			return m_items.size();
		}

		T Get(std::size_t const index)
		{
			return m_items[index & (Size() - 1)];
		}

		void Put(std::size_t const index, T x)
		{
			m_items[index & (Size() - 1)] = x;
		}

		// Growing the array returns a new circular_array object and keeps a
		// linked list of all previous arrays. This is done because other threads
		// could still be accessing elements from the smaller arrays.
		CircularArray *Grow(std::size_t const top, std::size_t const bottom)
		{
			auto *const newArray = new CircularArray(Size() * 2);
			newArray->m_previous.reset(this);
			for (std::size_t i = top; i != bottom; i++)
			{
				newArray->Put(i, Get(i));
			}
			return newArray;
		}
	};

	alignas(kCacheLineSize) std::atomic<std::uint64_t> m_top;
	alignas(kCacheLineSize) std::atomic<std::uint64_t> m_bottom;
	alignas(kCacheLineSize) std::atomic<CircularArray *> m_array;

public:
	void Push(T value)
	{
		std::uint64_t b = m_bottom.load(std::memory_order_relaxed);
		std::uint64_t t = m_top.load(std::memory_order_acquire);
		CircularArray *array = m_array.load(std::memory_order_relaxed);

		if (b - t > array->Size() - 1)
		{
			/* Full queue. */
			array = array->Grow(t, b);
			m_array.store(array, std::memory_order_release);
		}
		array->Put(b, value);

		std::atomic_thread_fence(std::memory_order_release);

		m_bottom.store(b + 1, std::memory_order_relaxed);
	}

	bool Pop(T *value)
	{
		std::uint64_t b = m_bottom.load(std::memory_order_relaxed) - 1;
		CircularArray *const array = m_array.load(std::memory_order_relaxed);
		m_bottom.store(b, std::memory_order_relaxed);

		std::atomic_thread_fence(std::memory_order_seq_cst);

		std::uint64_t t = m_top.load(std::memory_order_relaxed);
		bool result = true;
		if (t <= b)
		{
			// Non-empty queue.
			*value = array->Get(b);
			if (t == b)
			{
				// Single last element in queue.
				if (!std::atomic_compare_exchange_strong_explicit(&m_top, &t, t + 1, std::memory_order_seq_cst,
					std::memory_order_relaxed))
				{
					// Failed race.
					result = false;
				}
				m_bottom.store(b + 1, std::memory_order_relaxed);
			}
		}
		else
		{
			// Empty queue.
			result = false;
			m_bottom.store(b + 1, std::memory_order_relaxed);
		}

		return result;
	}

	bool Steal(T *const value)
	{
		std::uint64_t t = m_top.load(std::memory_order_acquire);

		std::atomic_thread_fence(std::memory_order_seq_cst);

		std::uint64_t const b = m_bottom.load(std::memory_order_acquire);
		if (t < b)
		{
			// Non-empty queue.
			CircularArray *const array = m_array.load(std::memory_order_consume);
			*value = array->Get(t);
			return std::atomic_compare_exchange_strong_explicit(&m_top, &t, t + 1, std::memory_order_seq_cst, std::memory_order_relaxed);
		}

		return false;
	}
};
