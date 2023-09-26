// 2018 Mitchell Andrews
#pragma once
#include <iostream>
#include <fstream>
#include "Singleton.h"
#include <vector>
/*
Logger.h
A utility class for creating and managing logs for the engine. You can change the
log file name and priority levels to control what info gets saved and where.
*/

/// Looks like things fucked up
#define YIKES(name) CLog::Log(CLog::LogType::Error, name)
#define YIKES_FMT( name, ... ) CLog::LogFmt( CLog::LogType::Error, \
                                             std::string(name) + "\n\t" + __FILE__ + \
                                             ":" + std::to_string(__LINE__) + \
                                             "]\n\t[" + __FUNCTION__ + "]\n\t", \
                                             __VA_ARGS__ )
/// A Warning
#define BRUH(name) CLog::Log(CLog::LogType::Warning, name)
#define BRUH_FMT( name, ... ) CLog::LogFmt( CLog::LogType::Warning, \
                                             std::string(name) + "\n\t" + __FILE__ + \
                                             ":" + std::to_string(__LINE__) + \
                                             "]\n\t[" + __FUNCTION__ + "]\n\t", \
                                             __VA_ARGS__ )

class CLog
{
public:
    ~CLog();

    enum class LogType : int
    {
        None = 0,
        Info,
        Trace,
        Debug,
        Warning,
        Error
    };

    enum class LogFilter : int
    {
        None = 0,
        Info = 0 << 0,
        Trace = 0 << 1,
        Debug = 0 << 2,
        Warning = 0 << 3,
        Error = 0 << 4
    };

    void SetLogFile( const std::string& filename );
    void SetLogVerbosity( CLog::LogType priority );

    bool LogMessage( CLog::LogType priority, std::string message );
    static bool Log( CLog::LogType priority, const std::string& message );

    template<typename... Args>
    static bool LogFmt( CLog::LogType priority, const std::string& message, Args&&... args );
    struct LogEntry
    {
        LogType Type = LogType::None;
        std::string Message;
    };
    std::string TypeToName( CLog::LogType );

    static std::vector<LogEntry> Messages;

private:

    std::ofstream mLogFile;
    std::string mLogFileLocation;
    LogType mPriority = LogType::None;
    CLog() = default;

    ME_SINGLETON_DEFINITION( CLog )
};

template<typename... Args>
bool CLog::LogFmt( LogType priority, const std::string& message, Args&&... args )
{
    size_t bufSize = std::snprintf( nullptr, 0, message.c_str(), std::forward<Args>( args )... ) + 1;

    std::vector<char> buf( bufSize );
    std::snprintf( buf.data(), bufSize, message.c_str(), std::forward<Args>( args )... );

    std::string formattedString = std::string( buf.data() );
    return CLog::GetInstance().LogMessage( priority, formattedString );
}
