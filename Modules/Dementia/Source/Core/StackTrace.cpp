#include "StackTrace.h"
#include "Dementia.h"
#include <sstream>

#if USING( ME_PLATFORM_WINDOWS )
#include <windows.h>
#if USING( ME_PLATFORM_WIN64 )
#include <DbgHelp.h>
#pragma comment(lib, "Dbghelp.lib")
#endif
#else
#include <execinfo.h>
#include <dlfcn.h>
#include <cxxabi.h>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <unistd.h>
#if USING( ME_PLATFORM_MACOS )
#include <sys/sysctl.h>
#include <sys/types.h>
#endif
#endif

namespace StackTrace
{
#if USING( ME_PLATFORM_WIN64 )
    std::string CaptureString( int skipFrames, int maxFrames )
    {
        std::ostringstream output;
        void* stack[128];
        maxFrames = maxFrames > 128 ? 128 : maxFrames;

        HANDLE process = GetCurrentProcess();
        SymInitialize( process, NULL, TRUE );
        const unsigned short frames = CaptureStackBackTrace( static_cast<DWORD>( skipFrames + 1 ), static_cast<DWORD>( maxFrames ), stack, NULL );

        SYMBOL_INFO* symbol = (SYMBOL_INFO*)calloc( sizeof( SYMBOL_INFO ) + 256, 1 );
        symbol->MaxNameLen = 255;
        symbol->SizeOfStruct = sizeof( SYMBOL_INFO );

        IMAGEHLP_LINE64 lineInfo = { sizeof( IMAGEHLP_LINE64 ) };
        for( unsigned short i = 0; i < frames; ++i )
        {
            const DWORD64 address = (DWORD64)( stack[i] );
            const bool hasSymbol = SymFromAddr( process, address, 0, symbol ) == TRUE;

            DWORD displacement = 0;
            output << "  #" << i << " " << ( hasSymbol ? symbol->Name : "???" );
            if( SymGetLineFromAddr64( process, address, &displacement, &lineInfo ) )
            {
                output << " (" << lineInfo.FileName << ":" << lineInfo.LineNumber << ")";
            }
            output << "\n";
        }

        free( symbol );
        SymCleanup( process );
        return output.str();
    }
#elif USING( ME_PLATFORM_WINDOWS )
    std::string CaptureString( int skipFrames, int maxFrames )
    {
        return "  (stack traces are unavailable on this platform)\n";
    }
#else
    std::string CaptureString( int skipFrames, int maxFrames )
    {
        std::ostringstream output;
        void* stack[128];
        maxFrames = maxFrames > 128 ? 128 : maxFrames;

        const int frames = backtrace( stack, maxFrames );
        for( int i = skipFrames + 1; i < frames; ++i )
        {
            Dl_info info{};
            output << "  #" << ( i - skipFrames - 1 ) << " ";
            if( dladdr( stack[i], &info ) && info.dli_sname )
            {
                int status = 0;
                char* demangled = abi::__cxa_demangle( info.dli_sname, nullptr, nullptr, &status );
                output << ( status == 0 && demangled ? demangled : info.dli_sname );
                std::free( demangled );

                const char* module = info.dli_fname ? std::strrchr( info.dli_fname, '/' ) : nullptr;
                output << " [" << ( module ? module + 1 : ( info.dli_fname ? info.dli_fname : "?" ) ) << "]";
            }
            else
            {
                output << stack[i];
                if( info.dli_fname )
                {
                    output << " [" << info.dli_fname << "]";
                }
            }
            output << "\n";
        }
        return output.str();
    }
#endif
}

namespace Debugger
{
#if USING( ME_PLATFORM_WINDOWS )
    bool IsAttached()
    {
        return IsDebuggerPresent() == TRUE;
    }
#elif USING( ME_PLATFORM_MACOS )
    bool IsAttached()
    {
        int mib[4] = { CTL_KERN, KERN_PROC, KERN_PROC_PID, getpid() };
        struct kinfo_proc info {};
        size_t size = sizeof( info );
        if( sysctl( mib, 4, &info, &size, nullptr, 0 ) != 0 )
        {
            return false;
        }
        return ( info.kp_proc.p_flag & P_TRACED ) != 0;
    }
#else
    bool IsAttached()
    {
        std::ifstream status( "/proc/self/status" );
        std::string line;
        while( std::getline( status, line ) )
        {
            if( line.rfind( "TracerPid:", 0 ) == 0 )
            {
                return std::atoi( line.c_str() + 10 ) != 0;
            }
        }
        return false;
    }
#endif
}
