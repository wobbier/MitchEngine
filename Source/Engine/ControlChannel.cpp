#include "PCH.h"
#include "Engine/ControlChannel.h"
#include "CLog.h"
#include "Core/CommandLine.h"
#include "Engine/DevConsole.h"
#include "JSON.h"
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <mutex>
#if !USING( ME_PLATFORM_WINDOWS )
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#endif


bool ControlChannel::Init()
{
    if( !CommandLine::Has( "--control-stdin" ) )
    {
        return false;
    }
#if USING( ME_PLATFORM_WINDOWS )
    CLog::Log( CLog::LogType::Warning, "Control: --control-stdin isn't supported on Windows yet" );
    return false;
#else
    const int flags = fcntl( STDIN_FILENO, F_GETFL, 0 );
    if( flags < 0 || fcntl( STDIN_FILENO, F_SETFL, flags | O_NONBLOCK ) < 0 )
    {
        CLog::Log( CLog::LogType::Warning, "Control: stdin can't be read without blocking; --control-stdin is off" );
        return false;
    }
    m_active = true;
    // The driver reads the log off stdout as it happens: a line at a time, not when a pipe's buffer fills.
    std::fflush( stdout );
    std::setvbuf( stdout, nullptr, _IOLBF, 0 );
    CLog::Log( CLog::LogType::Info, "Control: reading console lines from stdin (--control-stdin)" );
    return true;
#endif
}


std::string ControlChannel::FormatReply( unsigned long long InId, const std::string& InOutput )
{
    // A JSON string keeps a multi-line answer on one line; invalid UTF-8 is replaced, not thrown.
    return "[control] " + std::to_string( InId ) + " " + json( InOutput ).dump( -1, ' ', false, json::error_handler_t::replace );
}


std::string ControlChannel::ParseLine( const std::string& InLine, unsigned long long& OutId )
{
    OutId = 0;
    const size_t start = InLine.find_first_not_of( " \t\r" );
    if( start == std::string::npos )
    {
        return {};
    }
    size_t end = start;
    while( end < InLine.size() && std::isdigit( static_cast<unsigned char>( InLine[end] ) ) )
    {
        ++end;
    }
    std::string command = InLine.substr( start );
    // "<digits> <command>" (or the digits alone: an id with nothing to run). Digits glued to a word are a command.
    if( end > start && ( end == InLine.size() || InLine[end] == ' ' || InLine[end] == '\t' ) )
    {
        OutId = std::strtoull( InLine.c_str() + start, nullptr, 10 );
        command = InLine.substr( end );
    }
    const size_t first = command.find_first_not_of( " \t" );
    command = first == std::string::npos ? std::string() : command.substr( first );
    while( !command.empty() && ( command.back() == '\r' || command.back() == ' ' || command.back() == '\t' ) )
    {
        command.pop_back();
    }
    return command;
}


void ControlChannel::RunLine( const std::string& InLine )
{
    if( InLine.find_first_not_of( " \t\r" ) == std::string::npos )
    {
        return;
    }
    unsigned long long id = 0;
    const std::string command = ParseLine( InLine, id );
    // A machine-read answer (a Net Debugger view is ~10 KB, a few times a second) is logged by its size.
    const std::string output = DevConsole::Get().Execute( command, kLogLimit );
    // Straight to stdout (not the log's prefix and colours): the driver reads it there. Under the
    // log's lock, as one write: another thread's log line can't land inside the answer's line.
    const std::string reply = FormatReply( id, output ) + "\n";
    std::lock_guard<std::recursive_mutex> lock( CLog::GetInstance().GetMutex() );
    std::cout.write( reply.data(), static_cast<std::streamsize>( reply.size() ) );
    std::cout.flush();
}


bool ControlChannel::Poll()
{
    if( !m_active || m_closed )
    {
        return m_closed;
    }
#if !USING( ME_PLATFORM_WINDOWS )
    char buffer[4096];
    for( ;; )
    {
        const ssize_t got = read( STDIN_FILENO, buffer, sizeof( buffer ) );
        if( got > 0 )
        {
            m_pending.append( buffer, static_cast<size_t>( got ) );
            continue;
        }
        if( got == 0 )
        {
            m_closed = true;
        }
        else if( errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR )
        {
            m_closed = true;
        }
        break;
    }
#endif
    size_t newline = 0;
    while( ( newline = m_pending.find( '\n' ) ) != std::string::npos )
    {
        const std::string line = m_pending.substr( 0, newline );
        m_pending.erase( 0, newline + 1 );
        RunLine( line );
    }
    if( m_closed )
    {
        if( !m_pending.empty() )
        {
            RunLine( m_pending );
            m_pending.clear();
        }
        CLog::Log( CLog::LogType::Info, "Control: stdin closed; quitting" );
    }
    return m_closed;
}
