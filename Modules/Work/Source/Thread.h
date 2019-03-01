#pragma once
#include <wtypes.h>
#include <atomic>
#include <process.h>

struct Thread
{
	HANDLE Handle;
	DWORD Id;
};

using ThreadStartDef = unsigned int(__stdcall*)(void* Arg);

inline bool CreateThread(unsigned int const stackSize, ThreadStartDef const startFunc, void* const  arg, Thread* const returnThread)
{
	HANDLE const handle = reinterpret_cast<HANDLE>(_beginthreadex(nullptr, stackSize, startFunc, arg, 0u, nullptr));

	returnThread->Handle = handle;
	returnThread->Id = GetThreadId(handle);

	return handle != nullptr;
}

inline bool CreateThread(unsigned int const stackSize, ThreadStartDef const startFunc, void* const arg, std::size_t const coreAffinity, Thread* const returnThread)
{
	HANDLE const handle = reinterpret_cast<HANDLE>(_beginthreadex(nullptr, stackSize, startFunc, arg, CREATE_SUSPENDED, nullptr));

	if (!handle)
	{
		return false;
	}

	DWORD_PTR const mask = 1ULL << coreAffinity;
	SetThreadAffinityMask(handle, mask);

	returnThread->Handle = handle;
	returnThread->Id = GetThreadId(handle);
	ResumeThread(handle);

	return true;
}

inline Thread GetCurrentThreadInfo()
{
	Thread const result{ ::GetCurrentThread(), GetCurrentThreadId() };

	return result;
}