#include "Fiber.h"

Fiber::Fiber(std::size_t const stackSize, FiberStartRoutine const startRoutine, void* const arg)
	: Arg(arg)
	, Function(startRoutine)
{
	StackSize = stackSize;
	Stack = malloc(StackSize);
	Context = boost::context::detail::make_fcontext(static_cast<char*>(Stack) + StackSize, StackSize, startRoutine);
}

void Fiber::SwitchToFiber(Fiber* const fiber)
{
	boost::context::detail::transfer_t newContext = boost::context::detail::jump_fcontext(fiber->Context, fiber->Arg);
	Arg = fiber->Arg;
	//Context = newContext.fctx;
}

void Fiber::Reset(FiberStartRoutine const jobFunc, void* const args)
{
	Context = boost::context::detail::make_fcontext(static_cast<char*>(Stack) + StackSize, StackSize, jobFunc);
	Arg = args;
}

void Fiber::FiberFunc(boost::context::detail::transfer_t arg)
{
	Fiber* fiber = reinterpret_cast<Fiber*>(arg.data);
	if (fiber->Function)
	{
		//fiber->Function(fiber->Arg);
	}
}

void Fiber::Swap(Fiber& first, Fiber& second) noexcept
{
	std::swap(first.Stack, second.Stack);
	std::swap(first.StackSize, second.StackSize);
	std::swap(first.Context, second.Context);
	std::swap(first.Arg, second.Arg);
}
