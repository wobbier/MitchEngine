#include "PCH.h"
#include "Engine/QuitSignals.h"
#include "Dementia.h"

#include <atomic>
#if !USING( ME_PLATFORM_WINDOWS )
#include <csignal>
#endif

namespace
{
    std::atomic<int> gSignal{ 0 };

#if !USING( ME_PLATFORM_WINDOWS )
    void OnQuitSignal( int InSignal )
    {
        // Async-signal-safe: remember it; the main loop acts on it.
        gSignal.store( InSignal );
    }
#endif
}


void QuitSignals::Install()
{
#if !USING( ME_PLATFORM_WINDOWS )
    struct sigaction action = {};
    action.sa_handler = &OnQuitSignal;
    sigemptyset( &action.sa_mask );
    action.sa_flags = SA_RESTART;
    sigaction( SIGINT, &action, nullptr );
    sigaction( SIGTERM, &action, nullptr );
#endif
}


bool QuitSignals::Requested()
{
    return gSignal.load() != 0;
}


const char* QuitSignals::Name()
{
#if !USING( ME_PLATFORM_WINDOWS )
    switch( gSignal.load() )
    {
    case SIGINT: return "SIGINT";
    case SIGTERM: return "SIGTERM";
    default: break;
    }
#endif
    return gSignal.load() != 0 ? "signal" : "";
}
