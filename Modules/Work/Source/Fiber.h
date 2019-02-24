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

	Fiber(std::size_t const stackSize, FiberStartRoutine const startRoutine, void* const arg)
		: Arg(arg)
	{
		StackSize = stackSize;
		Stack = malloc(StackSize);
		Context = boost::context::detail::make_fcontext(static_cast<char*>(Stack) + StackSize, StackSize, startRoutine);
	}

	void SwitchToFiber(Fiber* const fiber)
	{
		if (Context != fiber->Context)
		{
			boost::context::detail::jump_fcontext(fiber->Context, fiber->Arg);
		}
	}

	void Reset(FiberStartRoutine const jobFunc, void* const args)
	{
		Context = boost::context::detail::make_fcontext(static_cast<char*>(Stack) + StackSize, StackSize, jobFunc);
		Arg = args;
	}

private:
	void* Stack = nullptr;
	void* Arg = nullptr;
	std::size_t StackSize = 0;
	boost::context::detail::fcontext_t Context;

	static void Swap(Fiber& first, Fiber& second) noexcept
	{
		std::swap(first.Stack, second.Stack);
		std::swap(first.StackSize, second.StackSize);
		std::swap(first.Context, second.Context);
		std::swap(first.Arg, second.Arg);
	}
};