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

/// A Warning
#define BRUH(name) CLog::Log(CLog::LogType::Warning, name)
#define BRUH_FMT( name, ... ) CLog::Log2( CLog::LogType::Warning, name, __VA_ARGS__ )

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
    static bool Log2( CLog::LogType priority, const std::string& message, Args&&... args );
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
bool CLog::Log2( LogType priority, const std::string& message, Args&&... args )
{
    //std::format( message, std::forward<Args>( args )... );
    std::printf( message.c_str(), std::forward<Args>( args )... );
    std::cout << std::endl;
    return true;
}
