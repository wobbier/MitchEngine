// 2018 Mitchell Andrews
#pragma once
#include <iostream>
#include <fstream>
#include "Singleton.h"
#include <vector>
#include <format>
#include <mutex>
#include <cstdint>
/*
CLog.h
A utility class for creating and managing logs for the engine. You can change the
log file name and priority levels to control what info gets saved and where.
Thread safe: messages may be logged from any thread.
*/

/// Looks like things fucked up
#define YIKES(name) CLog::Log(CLog::LogType::Error, name)
#define YIKES_FMT( name, ... ) CLog::LogFmt( CLog::LogType::Error, \
                                             std::string(name) + "\n\t" + __FILE__ + \
                                             ":" + std::to_string(__LINE__) + \
                                             "]\n\t[" + __FUNCTION__ + "]\n\t", \
                                             __VA_ARGS__ )
#define YIKES_NEW( ... ) CLog::Log( CLog::LogType::Error, \
                                           std::format("{}\n\t[{}:{}]\n\t[{}]\n\t", \
                                           CLog::FormatMessage2(__VA_ARGS__), __FILE__, __LINE__, __FUNCTION__) )
/// A Warning
#define BRUH(name) CLog::Log(CLog::LogType::Warning, name)
#define BRUH_FMT( name, ... ) CLog::LogFmt( CLog::LogType::Warning, \
                                             std::string(name) + "\n\t" + __FILE__ + \
                                             ":" + std::to_string(__LINE__) + \
                                             "]\n\t[" + __FUNCTION__ + "]\n\t", \
                                             __VA_ARGS__ )
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


#define BRUH_NEW( ... ) CLog::Log( CLog::LogType::Warning, \
                                           std::format("{}\n\t[{}:{}]\n\t[{}]\n\t", \
                                           CLog::FormatMessage2(__VA_ARGS__), __FILE__, __LINE__, __FUNCTION__) )
/// A Debug Message
#define DBG( ... ) CLog::Log( CLog::LogType::Debug, \
                                           std::format("{}\n\t[{}:{}]\n\t[{}]\n\t", \
                                           CLog::FormatMessage2(__VA_ARGS__), __FILE__, __LINE__, __FUNCTION__) )
class CLog
{
public:
    ~CLog();

    // Ordered by severity: verbosity filtering drops anything below the configured level.
    enum class LogType : int
    {
        None = 0,
        Trace,
        Debug,
        Info,
        Warning,
        Error
    };

    enum class LogFilter : int
    {
        None = 0,
        Trace = 1 << 0,
        Debug = 1 << 1,
        Info = 1 << 2,
        Warning = 1 << 3,
        Error = 1 << 4,
        All = Trace | Debug | Info | Warning | Error
    };

    static LogFilter ToFilter( LogType type );

    void SetLogFile( const std::string& filename );
    void SetLogVerbosity( CLog::LogType priority );

    bool LogMessage( CLog::LogType priority, std::string message );
    static bool Log( CLog::LogType priority, const std::string& message );

    // Pushes buffered file output to disk. Called automatically for warnings and errors.
    void Flush();

    template<typename... Args>
    static bool LogFmt( CLog::LogType priority, const std::string& message, Args&&... args );

    template<typename T>
    static std::string FormatMessage2( T&& single );

    template<typename... Args>
    static std::string FormatMessage2( std::format_string<Args...> fmt, Args&&... args );

    struct LogEntry
    {
        LogType Type = LogType::None;
        std::string Message;
        // Seconds since the logger was created.
        double Timestamp = 0.0;
        uint32_t ThreadId = 0;
    };
    std::string TypeToName( CLog::LogType );

    // Editor console history. Bounded; lock GetMutex() while reading it from the UI.
    static std::vector<LogEntry> Messages;
    static constexpr size_t kMaxMessages = 20000;

    // Total messages ever appended to Messages (keeps counting after trimming).
    uint64_t GetTotalMessageCount() const { return mTotalMessages; }

    std::recursive_mutex& GetMutex() { return mMutex; }

private:

    std::ofstream mLogFile;
    std::string mLogFileLocation;
    LogType mPriority = LogType::None;
    std::recursive_mutex mMutex;
    uint64_t mTotalMessages = 0;
    bool mUseColor = false;
    CLog();

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
std::string CLog::FormatMessage2( T&& single )
{
    return std::format( "{}", std::forward<T>( single ) );
}

template<typename... Args>
std::string CLog::FormatMessage2( std::format_string<Args...> fmt, Args&&... args )
{
    return std::format( fmt, std::forward<Args>( args )... );
}
