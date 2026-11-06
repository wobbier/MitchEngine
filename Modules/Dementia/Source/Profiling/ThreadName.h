#pragma once
#include <optick.h>

namespace Profiling
{
    // Names the calling thread for the OS (gdb, top -H, Windows debuggers). Linux keeps 15 characters.
    void SetOSThreadName( const char* InName );
}

// Every thread the engine starts calls this first, in the thread function's own scope (ME-321): it
// registers the thread's profiler lane for the thread's lifetime (OPTICK_THREAD is a scoped object),
// so the lane shows in every capture even while the thread is idle, and names the OS thread.
#define ME_PROFILE_THREAD( name ) \
    OPTICK_THREAD( name );        \
    ::Profiling::SetOSThreadName( name )
