#include "PCH.h"
#include "FrameCapture.h"

#include "CLog.h"
#include "optick.h"

#include <algorithm>
#include <ctime>
#include <filesystem>
#include <vector>

namespace
{
    constexpr const char* kCaptureFolder = ".tmp/Captures";

#if USING( ME_PLATFORM_LINUX )
    // The ChromeTrace shim writes Chrome trace JSON (chrome://tracing, Perfetto).
    constexpr const char* kCaptureExtension = ".json";
#else
    constexpr const char* kCaptureExtension = ".opt";
#endif


    // Deletes all but the newest kCapturesKept captures (capture_*).
    void PruneCaptures()
    {
        std::error_code ec;
        std::vector<std::filesystem::directory_entry> captures;
        for( const auto& entry : std::filesystem::directory_iterator( kCaptureFolder, ec ) )
        {
            if( entry.is_regular_file( ec ) && entry.path().filename().string().rfind( "capture_", 0 ) == 0 )
            {
                captures.push_back( entry );
            }
        }
        if( captures.size() <= FrameCapture::kCapturesKept )
        {
            return;
        }
        std::sort( captures.begin(), captures.end(), []( const auto& a, const auto& b ) {
            std::error_code timeError;
            return a.last_write_time( timeError ) > b.last_write_time( timeError );
        } );
        for( std::size_t i = FrameCapture::kCapturesKept; i < captures.size(); ++i )
        {
            std::filesystem::remove( captures[i].path(), ec );
        }
    }
}


std::string FrameCapture::CapturePath( const std::string& InFileName )
{
    std::error_code ec;
    std::filesystem::create_directories( kCaptureFolder, ec );
    return std::string( kCaptureFolder ) + "/" + InFileName;
}


void FrameCapture::Update( bool InStartPressed )
{
    static int s_captureFramesLeft = 0;
    if( InStartPressed && s_captureFramesLeft == 0 )
    {
        OPTICK_START_CAPTURE();
        s_captureFramesLeft = kCaptureFrames;
        CLog::GetInstance().Log( CLog::LogType::Info, "Profiler capture started (" + std::to_string( kCaptureFrames ) + " frames)" );
    }

    if( s_captureFramesLeft > 0 )
    {
        --s_captureFramesLeft;
        if( s_captureFramesLeft == 0 )
        {
            OPTICK_STOP_CAPTURE();
            std::string filename = std::string( "capture" ) + kCaptureExtension;
            const std::time_t t = std::time( nullptr );
            std::tm localTime{};
#if USING( ME_PLATFORM_WINDOWS )
            if( localtime_s( &localTime, &t ) == 0 )
#else
            if( localtime_r( &t, &localTime ) )
#endif
            {
                char stamp[64] = {};
                std::strftime( stamp, sizeof( stamp ), "capture_%Y%m%d_%H%M%S", &localTime );
                filename = std::string( stamp ) + kCaptureExtension;
            }
            const std::string path = CapturePath( filename );
            OPTICK_SAVE_CAPTURE( path.c_str() );
            PruneCaptures();
            CLog::GetInstance().Log( CLog::LogType::Info, "Profiler capture saved: " + path );
        }
    }
}
