#include "CLog.h"
#include "Dementia.h"
#include <string>
#include <chrono>
#include <thread>
#include <cstdio>
#if USING( ME_PLATFORM_WIN64 )
#include <wtypes.h>
#include <wincon.h>
#include <processenv.h>
#elif !USING( ME_PLATFORM_WINDOWS )
#include <unistd.h>
#endif

#if USING( ME_EDITOR )
std::vector<CLog::LogEntry> CLog::Messages;
#endif

namespace
{
    const auto kLogStartTime = std::chrono::steady_clock::now();

    uint32_t CurrentThreadId()
    {
        return static_cast<uint32_t>( std::hash<std::thread::id>()( std::this_thread::get_id() ) & 0xFFFF );
    }
}


CLog::CLog()
{
#if !USING( ME_PLATFORM_WINDOWS )
    mUseColor = isatty( fileno( stdout ) ) != 0;
#endif
}


CLog::~CLog()
{
    std::lock_guard<std::recursive_mutex> lock( mMutex );
    if( mLogFile.is_open() )
    {
        mLogFile.flush();
        mLogFile.close();
    }
}


CLog::LogFilter CLog::ToFilter( LogType type )
{
    switch( type )
    {
    case LogType::Trace: return LogFilter::Trace;
    case LogType::Debug: return LogFilter::Debug;
    case LogType::Info: return LogFilter::Info;
    case LogType::Warning: return LogFilter::Warning;
    case LogType::Error: return LogFilter::Error;
    default: return LogFilter::None;
    }
}


void CLog::SetLogFile( const std::string& filename )
{
    std::lock_guard<std::recursive_mutex> lock( mMutex );
    if( mLogFile.is_open() )
    {
        mLogFile.close();
    }
    mLogFileLocation = filename;
    // Truncate: each run starts a fresh log.
    mLogFile.open( mLogFileLocation, std::ios_base::out | std::ios_base::trunc );
}


void CLog::SetLogVerbosity( CLog::LogType priority )
{
    mPriority = priority;
}


bool CLog::LogMessage( CLog::LogType priority, std::string message )
{
    if( mPriority == LogType::None ) return false;
    if( priority < mPriority )
        return false;

    const double timestamp = std::chrono::duration<double>( std::chrono::steady_clock::now() - kLogStartTime ).count();
    const uint32_t threadId = CurrentThreadId();

    int color = 15;
    const char* ansiColor = "\033[0m";
    std::string type;
    switch( priority )
    {
    case LogType::Info:
        type = "[Info]: ";
        color = 8;
        ansiColor = "\033[0m";
        break;
    case LogType::Trace:
        type = "[Trace]: ";
        ansiColor = "\033[90m";
        break;
    case LogType::Debug:
        type = "[Debug]: ";
        ansiColor = "\033[36m";
        break;
    case LogType::Warning:
        type = "[Warning]: ";
        color = 14;
        ansiColor = "\033[33m";
        break;
    case LogType::Error:
        type = "[! Error !]: ";
        color = 12;
        ansiColor = "\033[1;31m";
        break;
    default:
        type = "[Unknown]: ";
        break;
    }

    char prefix[32];
    std::snprintf( prefix, sizeof( prefix ), "[%9.3f][%04x]", timestamp, threadId );

    std::lock_guard<std::recursive_mutex> lock( mMutex );

    if( mLogFile.is_open() )
    {
        mLogFile << prefix << type << message << '\n';
        if( priority >= LogType::Warning )
        {
            mLogFile.flush();
        }
    }

#if USING( ME_PLATFORM_WIN64 )
    HANDLE hConsole = GetStdHandle( STD_OUTPUT_HANDLE );
    SetConsoleTextAttribute( hConsole, static_cast<WORD>( color ) );
    std::cout << type << message << std::endl;
    SetConsoleTextAttribute( hConsole, 15 );
#else
    if( mUseColor )
    {
        std::cout << ansiColor << type << message << "\033[0m\n";
    }
    else
    {
        std::cout << type << message << '\n';
    }
    if( priority >= LogType::Warning )
    {
        std::cout.flush();
    }
#endif

#if USING( ME_EDITOR )
    if( Messages.size() >= kMaxMessages )
    {
        // Trim in chunks so the erase cost is amortized.
        Messages.erase( Messages.begin(), Messages.begin() + kMaxMessages / 10 );
    }
    Messages.emplace_back( LogEntry { priority, std::move( message ), timestamp, threadId } );
#endif
    ++mTotalMessages;

    return true;
}


bool CLog::Log( LogType priority, const std::string& message )
{
    return CLog::GetInstance().LogMessage( priority, message );
}


void CLog::Flush()
{
    std::lock_guard<std::recursive_mutex> lock( mMutex );
    if( mLogFile.is_open() )
    {
        mLogFile.flush();
    }
    std::cout.flush();
}


std::string CLog::TypeToName( LogType type )
{
    switch( type )
    {
    case LogType::None: return "None";
    case LogType::Info: return "Info";
    case LogType::Trace: return "Trace";
    case LogType::Debug: return "Debug";
    case LogType::Warning: return "Warning";
    case LogType::Error: return "Error";
    default: return "Unknown";
    }
}
