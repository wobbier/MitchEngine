#pragma once
#include <boost/context/detail/fcontext.hpp>

using FiberStartRoutine = void(*)(boost::context::detail::transfer_t arg);

class Fiber
{
public:
	Fiber() = default;
	Fiber(Fiber const& other) = delete;
	Fiber& operator=(Fiber const& other) = delete;
	Fiber(Fiber&& other) noexcept : Fiber()
	{
		Swap(*this, other);
	}
	Fiber& operator=(Fiber&& other) noexcept
	{
		Swap(*this, other);
		return *this;
	}
	~Fiber()
	{
		if (Stack != nullptr)
		{
			void(static_cast<char *>(Stack));
			void(static_cast<char *>(Stack) + StackSize);
			free(Stack);
		}
	}

	Fiber(std::size_t const stackSize, FiberStartRoutine const startRoutine, void* const arg);

	void SwitchToFiber(Fiber* const fiber);

	void Reset(FiberStartRoutine const jobFunc, void* const args);

	static void FiberFunc(boost::context::detail::transfer_t arg);

private:
	void* Stack = nullptr;
	void* Arg = nullptr;
	std::size_t StackSize = 0;
	boost::context::detail::fcontext_t Context;
	FiberStartRoutine Function;
	static void Swap(Fiber& first, Fiber& second) noexcept;
};