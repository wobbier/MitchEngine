#include "ThreadName.h"
#include "Dementia.h"
#include <string>

#if USING( ME_PLATFORM_WINDOWS )
#include <windows.h>
#else
#include <pthread.h>
#endif

namespace Profiling
{
    void SetOSThreadName( const char* InName )
    {
        if( !InName )
        {
            return;
        }
#if USING( ME_PLATFORM_WINDOWS )
        const std::string name( InName );
        const std::wstring wide( name.begin(), name.end() );
        SetThreadDescription( GetCurrentThread(), wide.c_str() );
#elif USING( ME_PLATFORM_MACOS )
        pthread_setname_np( InName );
#else
        // Linux: at most 15 characters plus the terminator, or the call fails.
        const std::string name = std::string( InName ).substr( 0, 15 );
        pthread_setname_np( pthread_self(), name.c_str() );
#endif
    }
}
