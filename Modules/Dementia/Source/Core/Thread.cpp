#include "Thread.h"
#include "optick.h"
#include <thread>
#include "Dementia.h"

#if USING( ME_PLATFORM_WINDOWS )
#include <Windows.h>
#include <winbase.h>
#include <processthreadsapi.h>
#endif

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

#if USING( ME_PLATFORM_WINDOWS )
    HANDLE threadHandle = ThreadFunc.native_handle();
    SetThreadPriority( threadHandle, THREAD_PRIORITY_ABOVE_NORMAL );
#endif
}

void Thread::Join()
{
    ThreadFunc.join();
}

void Thread::SignalShutdown()
{
    Kill = true;
}

bool Thread::IsAlive() const
{
    return !Kill;
}

