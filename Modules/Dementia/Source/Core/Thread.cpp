#include "Thread.h"
#include "optick.h"
#include <thread>

Thread::Thread()
{
}

void Thread::Create( std::function<void()> InFunc, const std::string& InName /*= ""*/ )
{
    ThreadName = InName;
    ThreadFunc = std::thread( [InFunc, InName]() {
        if( InFunc )
        {
            InFunc();
        }
        } );
}

void Thread::Join()
{
    ThreadFunc.join();
}

bool Thread::IsAlive() const
{
    return !Kill;
}

