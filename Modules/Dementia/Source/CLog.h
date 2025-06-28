// 2018 Mitchell Andrews
#pragma once
#include <iostream>
#include <fstream>
#include "Singleton.h"
#include <vector>
#include <format>
/*
CLog.h
A utility class for creating and managing logs for the engine. You can change the
log file name and priority levels to control what info gets saved and where.
*/

/// Looks like things fucked up
#define YIKES( ... ) CLog::Log( CLog::LogType::Error, \
                                           std::format("{}\n\t[{}:{}]\n\t[{}]\n\t", \
                                           CLog::FormatMessage2(__VA_ARGS__), __FILE__, __LINE__, __FUNCTION__) )


/// A Warning
#define BRUH( ... ) CLog::Log( CLog::LogType::Warning, \
                                           std::format("{}\n\t[{}:{}]\n\t[{}]\n\t", \
                                           CLog::FormatMessage2(__VA_ARGS__), __FILE__, __LINE__, __FUNCTION__) )
/// A Debug Message
#define DBG( ... ) CLog::Log( CLog::LogType::Debug, \
                                           std::format("{}\n\t[{}:{}]\n\t[{}]\n\t", \
                                           CLog::FormatMessage2(__VA_ARGS__), __FILE__, __LINE__, __FUNCTION__) )
/// Info log
#define INFO(category, message) \
    CLog::Log(CLog::LogType::Info, \
        std::string(category) + " [" + std::string(__FILE__).substr(std::string(__FILE__).find_last_of("/\\") + 1) + \
        ":" + std::to_string(__LINE__) + "] " + message)

/// Info log with category
#define INFO_FMT(category, formatStr, ...) \
    CLog::LogFmt(CLog::LogType::Info, \
        std::string(category) + " [" + \
        std::string(__FILE__).substr(std::string(__FILE__).find_last_of("/\\") + 1) + \
        ":" + std::to_string(__LINE__) + "] " + formatStr, \
        __VA_ARGS__)


class CLog
{
public:
    ~CLog();

    enum class LogType : int
    {
        None = 0,
        Error,
        Warning,
        Info,
        Success,
        Trace,
        Debug,
    };

    enum class LogFilter : int
    {
        None = 0,
        Info = 0 << 0,
        Trace = 0 << 1,
        Debug = 0 << 2,
        Warning = 0 << 3,
        Error = 0 << 4,
        Success = 0 << 4
    };

    void SetLogFile( const std::string& filename );
    void SetLogVerbosity( CLog::LogType priority );

    bool LogMessage( CLog::LogType priority, std::string message );
    static bool Log( CLog::LogType priority, const std::string& message );

    template<typename... Args>
    static bool LogFmt( CLog::LogType priority, const std::string& message, Args&&... args );

    template<typename T>
    static std::string FormatMessage2(T&& single);

    template<typename... Args>
    static std::string FormatMessage2(std::format_string<Args...> fmt, Args&&... args);


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

template<typename T>
std::string CLog::FormatMessage2(T&& single)
{
    return std::format("{}", std::forward<T>(single));
}

template<typename... Args>
std::string CLog::FormatMessage2(std::format_string<Args...> fmt, Args&&... args)
{
    return std::format(fmt, std::forward<Args>(args)...);
}
