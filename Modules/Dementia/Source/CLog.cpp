#include "CLog.h"
#include "Dementia.h"
#include <string>
#if USING( ME_PLATFORM_WIN64 )
#include <wtypes.h>
#include <wincon.h>
#include <processenv.h>
#endif

#if USING( ME_EDITOR )
std::vector<CLog::LogEntry> CLog::Messages;
#endif

CLog::~CLog()
{
}

void CLog::SetLogFile( const std::string& filename )
{
    mLogFileLocation = filename;
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


#if USING( ME_PLATFORM_WIN64 )
    mLogFile.open( mLogFileLocation, std::ios_base::app );

    int color = 15;
    std::string type;
    switch( priority )
    {
    case LogType::Info:
        type = "[Info]: ";
        color = 8;
        break;
    case LogType::Trace:
        type = "[Trace]: ";
        break;
    case LogType::Debug:
        type = "[Debug]: ";
        break;
    case LogType::Warning:
        type = "[Warning]: ";
        color = 14;
        break;
    case LogType::Error:
        type = "[! Error !]: ";
        color = 12;
        break;
    default:
        type = "[Unknown]: ";
        break;
    }

    HANDLE hConsole = GetStdHandle( STD_OUTPUT_HANDLE );
    SetConsoleTextAttribute( hConsole, static_cast<WORD>( color ) );

    mLogFile << type << message.c_str() << std::endl;
    std::cout << type << message.c_str() << std::endl;

    SetConsoleTextAttribute( hConsole, 15 );
    mLogFile.close();
#endif

#if USING( ME_EDITOR )
    Messages.emplace_back( LogEntry { priority, std::move( message ) } );
#endif

    return true;
}

bool CLog::Log( LogType priority, const std::string& message )
{
    return CLog::GetInstance().LogMessage( priority, message );
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
