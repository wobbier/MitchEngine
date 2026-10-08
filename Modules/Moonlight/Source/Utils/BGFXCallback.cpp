#include "BGFXCallback.h"
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


    void BGFXCallback::screenShot( const char* _filePath, uint32_t _width, uint32_t _height, uint32_t _pitch, bgfx::TextureFormat::Enum _format, const void* _data, uint32_t _size, bool _yflip )
    {
        std::filesystem::path outPath( _filePath );
        if( outPath.has_parent_path() )
        {
            std::error_code ec;
            std::filesystem::create_directories( outPath.parent_path(), ec );
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
