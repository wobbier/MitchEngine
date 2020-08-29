#include "Thread.h"
#include "optick.h"

Thread::Thread()
{
}

void Thread::Create(std::function<void()> InFunc, const std::string& InName /*= ""*/)
{
	ThreadFunc = std::thread([InFunc, InName]() {
		OPTICK_THREAD(InName.c_str());
		if (InFunc)
		{
			InFunc();
		}
	});
}

void Thread::Join()
{
	ThreadFunc.join();
}

bool Thread::IsAlive() const
{
	return !Kill;
}

