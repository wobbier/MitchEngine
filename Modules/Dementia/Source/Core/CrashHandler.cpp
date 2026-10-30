#include "CrashHandler.h"
#include "Dementia.h"
#include "CLog.h"
#include "Core/StackTrace.h"
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <exception>
#include <filesystem>
#include <fstream>
#include <algorithm>

#if USING( ME_PLATFORM_WIN64 )
#include <windows.h>
#include <DbgHelp.h>
#pragma comment(lib, "Dbghelp.lib")
#elif !USING( ME_PLATFORM_WINDOWS )
#include <csignal>
#include <execinfo.h>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace
{
    std::string s_reportPath;
    std::string s_dumpPath;
    std::atomic<bool> s_isHandlingCrash{ false };

    std::string MakeTimestamp()
    {
        const std::time_t now = std::time( nullptr );
        std::tm localTime{};
#if USING( ME_PLATFORM_WINDOWS )
        if( localtime_s( &localTime, &now ) != 0 )
#else
        if( !localtime_r( &now, &localTime ) )
#endif
        {
            return std::to_string( now );
        }
        char buffer[32] = {};
        std::strftime( buffer, sizeof( buffer ), "%Y%m%d_%H%M%S", &localTime );
        return buffer;
    }


    void OnTerminate()
    {
        std::string reason = "std::terminate called";
        if( std::exception_ptr current = std::current_exception() )
        {
            try
            {
                std::rethrow_exception( current );
            }
            catch( const std::exception& e )
            {
                reason = std::string( "Unhandled exception: " ) + e.what();
            }
            catch( ... )
            {
                reason = "Unhandled non-std exception";
            }
        }

        const std::string report = reason + "\nCall stack:\n" + StackTrace::CaptureString( 1 );
        CLog::Log( CLog::LogType::Error, report );
        CLog::GetInstance().Flush();

        std::ofstream file( s_reportPath );
        file << report;
        file.close();

        std::abort();
    }

#if USING( ME_PLATFORM_WIN64 )
    LONG WINAPI OnUnhandledException( EXCEPTION_POINTERS* exceptionInfo )
    {
        if( s_isHandlingCrash.exchange( true ) )
        {
            return EXCEPTION_CONTINUE_SEARCH;
        }

        HANDLE dumpFile = CreateFileA( s_dumpPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr );
        if( dumpFile != INVALID_HANDLE_VALUE )
        {
            MINIDUMP_EXCEPTION_INFORMATION info;
            info.ThreadId = GetCurrentThreadId();
            info.ExceptionPointers = exceptionInfo;
            info.ClientPointers = FALSE;
            MiniDumpWriteDump( GetCurrentProcess(), GetCurrentProcessId(), dumpFile, MiniDumpWithIndirectlyReferencedMemory, &info, nullptr, nullptr );
            CloseHandle( dumpFile );
        }

        char header[128];
        std::snprintf( header, sizeof( header ), "Unhandled exception 0x%08lX at %p", exceptionInfo->ExceptionRecord->ExceptionCode, exceptionInfo->ExceptionRecord->ExceptionAddress );
        const std::string report = std::string( header ) + "\nMinidump: " + s_dumpPath + "\nCall stack:\n" + StackTrace::CaptureString( 0 );

        std::ofstream file( s_reportPath );
        file << report;
        file.close();

        CLog::Log( CLog::LogType::Error, report );
        CLog::GetInstance().Flush();
        return EXCEPTION_EXECUTE_HANDLER;
    }
#elif !USING( ME_PLATFORM_WINDOWS )
    alignas( 16 ) char s_alternateStack[64 * 1024];

    const char* SignalName( int signal )
    {
        switch( signal )
        {
        case SIGSEGV: return "SIGSEGV (segmentation fault)";
        case SIGBUS: return "SIGBUS (bus error)";
        case SIGILL: return "SIGILL (illegal instruction)";
        case SIGFPE: return "SIGFPE (floating point exception)";
        case SIGABRT: return "SIGABRT (abort)";
        default: return "unknown signal";
        }
    }


    void WriteString( int fd, const char* text )
    {
        if( fd >= 0 )
        {
            const ssize_t ignored = write( fd, text, std::strlen( text ) );
            (void)ignored;
        }
    }


    void OnFatalSignal( int signal, siginfo_t* info, void* )
    {
        if( s_isHandlingCrash.exchange( true ) )
        {
            _exit( 128 + signal );
        }

        // Only async-signal-safe calls until the report is on disk.
        const int fd = open( s_reportPath.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0644 );
        char header[160];
        std::snprintf( header, sizeof( header ), "\n*** MitchEngine crashed: %s at address %p ***\nCall stack:\n", SignalName( signal ), info ? info->si_addr : nullptr );

        void* frames[128];
        const int frameCount = backtrace( frames, 128 );

        WriteString( STDERR_FILENO, header );
        backtrace_symbols_fd( frames, frameCount, STDERR_FILENO );
        if( fd >= 0 )
        {
            WriteString( fd, header );
            backtrace_symbols_fd( frames, frameCount, fd );
            close( fd );
        }

        WriteString( STDERR_FILENO, "Crash report: " );
        WriteString( STDERR_FILENO, s_reportPath.c_str() );
        WriteString( STDERR_FILENO, "\n" );

        // Best effort: get buffered log lines out before the process dies.
        CLog::GetInstance().Flush();

        // SA_RESETHAND restored the default action; re-raise for a core dump / correct exit code.
        raise( signal );
    }
#endif
}

namespace CrashHandler
{
    void SetDirectory( const std::string& crashDirectory )
    {
        std::error_code ec;
        std::filesystem::create_directories( crashDirectory, ec );

        // Time, then the process: two sessions in one second (a crash and the relaunch that reports
        // it) still get their own names.
#if USING( ME_PLATFORM_WIN64 )
        const std::string stamp = MakeTimestamp() + "_" + std::to_string( GetCurrentProcessId() );
#elif !USING( ME_PLATFORM_WINDOWS )
        const std::string stamp = MakeTimestamp() + "_" + std::to_string( getpid() );
#else
        const std::string stamp = MakeTimestamp();
#endif
        s_reportPath = ( std::filesystem::path( crashDirectory ) / ( "crash_" + stamp + ".txt" ) ).string();
        s_dumpPath = ( std::filesystem::path( crashDirectory ) / ( "crash_" + stamp + ".dmp" ) ).string();
    }


    void Install( const std::string& crashDirectory )
    {
        SetDirectory( crashDirectory );

        std::set_terminate( &OnTerminate );

#if USING( ME_PLATFORM_WIN64 )
        SetUnhandledExceptionFilter( &OnUnhandledException );
#elif !USING( ME_PLATFORM_WINDOWS )
        stack_t alternateStack{};
        alternateStack.ss_sp = s_alternateStack;
        alternateStack.ss_size = sizeof( s_alternateStack );
        sigaltstack( &alternateStack, nullptr );

        // Warm up backtrace() so its lazy libgcc load doesn't happen inside the handler.
        void* warmup[2];
        backtrace( warmup, 2 );

        struct sigaction action{};
        action.sa_sigaction = &OnFatalSignal;
        action.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_RESETHAND;
        sigemptyset( &action.sa_mask );
        for( int signal : { SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT } )
        {
            sigaction( signal, &action, nullptr );
        }
#endif
    }


    const std::string& GetReportPath()
    {
        return s_reportPath;
    }


    std::vector<std::string> FindNewReports()
    {
        namespace fs = std::filesystem;
        const fs::path directory = fs::path( s_reportPath ).parent_path();
        std::string acknowledged;
        {
            std::ifstream marker( directory / "Acknowledged.txt" );
            std::getline( marker, acknowledged );
        }
        std::vector<std::string> reports;
        std::error_code error;
        for( const fs::directory_entry& entry : fs::directory_iterator( directory, error ) )
        {
            const std::string name = entry.path().filename().string();
            // crash_<YYYYmmdd_HHMMSS>.txt sorts by time; this session's own isn't a previous one.
            if( name.rfind( "crash_", 0 ) == 0 && entry.path().extension() == ".txt" && name > acknowledged
                && entry.path().string() != s_reportPath )
            {
                reports.push_back( entry.path().string() );
            }
        }
        std::sort( reports.begin(), reports.end() );
        return reports;
    }


    void AcknowledgeReports()
    {
        const std::vector<std::string> reports = FindNewReports();
        if( reports.empty() )
        {
            return;
        }
        std::ofstream marker( std::filesystem::path( s_reportPath ).parent_path() / "Acknowledged.txt" );
        marker << std::filesystem::path( reports.back() ).filename().string() << "\n";
    }


    void CrashForTest()
    {
        CLog::Log( CLog::LogType::Warning, "Crash: --crash-test, crashing on purpose" );
        CLog::GetInstance().Flush();
#if USING( ME_PLATFORM_WIN64 )
        RaiseException( EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr );
#elif !USING( ME_PLATFORM_WINDOWS )
        raise( SIGSEGV );
#endif
        std::abort();
    }
}
