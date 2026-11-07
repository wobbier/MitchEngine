#include "BGFXCallback.h"
#include <algorithm>
#include <map>
#include <mutex>
#include "CLog.h"
#include <bimg/bimg.h>
#include <bx/file.h>
#include <cstdio>
#include <cstdarg>
#include <cstdlib>
#include <filesystem>

namespace Moonlight
{
    void BGFXCallback::fatal( const char* _filePath, uint16_t _line, bgfx::Fatal::Enum _code, const char* _str )
    {
        CLog::Log( CLog::LogType::Error, std::string( "[bgfx] FATAL " ) + _filePath + ":" + std::to_string( _line ) + " " + ( _str ? _str : "" ) );

        if( _code == bgfx::Fatal::DebugCheck )
        {
            return;
        }
        std::abort();
    }


    void BGFXCallback::traceVargs( const char* _filePath, uint16_t _line, const char* _format, va_list _argList )
    {
        if( !EnableTrace )
        {
            return;
        }

        char buffer[2048];
        std::vsnprintf( buffer, sizeof( buffer ), _format, _argList );
        CLog::Log( CLog::LogType::Debug, std::string( "[bgfx] " ) + buffer );
    }


    namespace
    {
        struct ScreenshotCrop
        {
            uint32_t X = 0, Y = 0, Width = 0, Height = 0;
        };
        std::mutex& CropMutex()
        {
            static std::mutex mutex;
            return mutex;
        }
        std::map<std::string, ScreenshotCrop>& Crops()
        {
            static std::map<std::string, ScreenshotCrop> crops;
            return crops;
        }
    }


    void BGFXCallback::SetScreenshotCrop( const std::string& InPath, uint32_t InX, uint32_t InY, uint32_t InWidth, uint32_t InHeight )
    {
        std::lock_guard<std::mutex> lock( CropMutex() );
        Crops()[InPath] = { InX, InY, InWidth, InHeight };
    }


    void BGFXCallback::ClearScreenshotCrop( const std::string& InPath )
    {
        std::lock_guard<std::mutex> lock( CropMutex() );
        Crops().erase( InPath );
    }


    void BGFXCallback::screenShot( const char* _filePath, uint32_t _width, uint32_t _height, uint32_t _pitch, bgfx::TextureFormat::Enum _format, const void* _data, uint32_t _size, bool _yflip )
    {
        std::filesystem::path outPath( _filePath );
        if( outPath.has_parent_path() )
        {
            std::error_code ec;
            std::filesystem::create_directories( outPath.parent_path(), ec );
        }

        // A requested crop (panel-screenshot): offset into the image and shrink it. Rows are stored
        // bottom-up when _yflip is set, so the crop's first stored row is counted from the bottom.
        {
            std::lock_guard<std::mutex> lock( CropMutex() );
            auto crop = Crops().find( _filePath );
            if( crop != Crops().end() )
            {
                const uint32_t bytesPerPixel = _width > 0 ? _pitch / _width : 4;
                const uint32_t x = std::min( crop->second.X, _width );
                const uint32_t y = std::min( crop->second.Y, _height );
                const uint32_t width = std::min( crop->second.Width, _width - x );
                const uint32_t height = std::min( crop->second.Height, _height - y );
                const uint32_t firstRow = _yflip ? _height - y - height : y;
                _data = static_cast<const uint8_t*>( _data ) + size_t( firstRow ) * _pitch + size_t( x ) * bytesPerPixel;
                _width = std::max( width, 1u );
                _height = std::max( height, 1u );
                Crops().erase( crop );
            }
        }

        bx::FileWriter writer;
        bx::Error err;
        if( bx::open( &writer, _filePath, false, &err ) )
        {
            bimg::imageWritePng( &writer, _width, _height, _pitch, _data, bimg::TextureFormat::Enum( _format ), _yflip, &err );
            bx::close( &writer );
            CLog::Log( CLog::LogType::Info, std::string( "Saved screenshot: " ) + _filePath );
        }
        else
        {
            CLog::Log( CLog::LogType::Error, std::string( "Failed to open screenshot path: " ) + _filePath );
        }

        m_screenshotCount.fetch_add( 1 );
    }
}
