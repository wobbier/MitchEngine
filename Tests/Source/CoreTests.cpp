#include <doctest/doctest.h>

#include "CLog.h"
#include "Dementia.h"
#include "Core/CommandLine.h"
#include "Core/CrashHandler.h"
#include "Core/StackTrace.h"
#include "Math/Vector3.h"
#include <atomic>
#include <filesystem>
#include <fstream>
#include <thread>
#include <vector>

#if !USING( ME_PLATFORM_WINDOWS )
#include <sys/wait.h>
#include <unistd.h>
#endif

TEST_CASE( "CommandLine parses flags and values" )
{
    const char* args[] = { "app", "--frames", "120", "--screenshot", "out.png", "--exit", "--scale", "1.5" };
    CommandLine::Set( 8, const_cast<char**>( args ) );

    CHECK( CommandLine::Has( "--frames" ) );
    CHECK( CommandLine::GetInt( "--frames" ) == 120 );
    CHECK( CommandLine::GetString( "--screenshot" ) == "out.png" );
    CHECK( CommandLine::Has( "--exit" ) );
    CHECK( CommandLine::GetString( "--exit", "none" ) == "none" );
    CHECK( CommandLine::GetFloat( "--scale" ) == doctest::Approx( 1.5f ) );
    CHECK_FALSE( CommandLine::Has( "--missing" ) );
    CHECK( CommandLine::GetInt( "--missing", 7 ) == 7 );
}


TEST_CASE( "StackTrace captures the calling function" )
{
    const std::string trace = StackTrace::CaptureString();
    CHECK_FALSE( trace.empty() );
#if !USING( ME_PLATFORM_WINDOWS )
    // -rdynamic exports test symbols, so the doctest runner should be visible in the stack.
    CHECK( trace.find( "doctest" ) != std::string::npos );
#endif
}


TEST_CASE( "CLog is safe to use from many threads" )
{
    CLog& log = CLog::GetInstance();
    const uint64_t before = log.GetTotalMessageCount();

    constexpr int kThreads = 8;
    constexpr int kMessagesPerThread = 250;
    std::vector<std::thread> threads;
    for( int t = 0; t < kThreads; ++t )
    {
        threads.emplace_back( [t]() {
            for( int i = 0; i < kMessagesPerThread; ++i )
            {
                CLog::Log( CLog::LogType::Warning, "thread " + std::to_string( t ) + " message " + std::to_string( i ) );
            }
        } );
    }
    for( std::thread& thread : threads )
    {
        thread.join();
    }

    CHECK( log.GetTotalMessageCount() - before == kThreads * kMessagesPerThread );
}


TEST_CASE( "CLog filter bits are distinct" )
{
    CHECK( static_cast<int>( CLog::ToFilter( CLog::LogType::Trace ) ) != static_cast<int>( CLog::ToFilter( CLog::LogType::Error ) ) );
    CHECK( ( static_cast<int>( CLog::LogFilter::All ) & static_cast<int>( CLog::ToFilter( CLog::LogType::Warning ) ) ) != 0 );
    CHECK( CLog::LogType::Trace < CLog::LogType::Info );
    CHECK( CLog::LogType::Warning < CLog::LogType::Error );
}


TEST_CASE( "Vector3 cross product" )
{
    const Vector3 x( 1.f, 0.f, 0.f );
    const Vector3 y( 0.f, 1.f, 0.f );
    const Vector3 z = x.Cross( y );
    CHECK( z.x == doctest::Approx( 0.f ) );
    CHECK( z.y == doctest::Approx( 0.f ) );
    CHECK( z.z == doctest::Approx( 1.f ) );
}


#if !USING( ME_PLATFORM_WINDOWS )
TEST_CASE( "CrashHandler writes a report when the process crashes" )
{
    const std::string crashDir = ".tmp/TestCrashes";
    std::error_code ec;
    std::filesystem::remove_all( crashDir, ec );

    const pid_t child = fork();
    REQUIRE( child >= 0 );
    if( child == 0 )
    {
        CrashHandler::Install( crashDir );
        volatile int* null = nullptr;
        *null = 42;
        _exit( 0 );
    }

    int status = 0;
    waitpid( child, &status, 0 );
    CHECK( WIFSIGNALED( status ) );

    bool foundReport = false;
    for( const auto& entry : std::filesystem::directory_iterator( crashDir, ec ) )
    {
        std::ifstream report( entry.path() );
        const std::string contents( ( std::istreambuf_iterator<char>( report ) ), std::istreambuf_iterator<char>() );
        foundReport |= contents.find( "SIGSEGV" ) != std::string::npos;
    }
    CHECK( foundReport );
}
#endif
