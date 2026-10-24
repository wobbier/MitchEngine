#include "ColorLut.h"
#include "Utils/BGFXUtils.h"
#include "CLog.h"
#include "VFS/VFS.h"
#include <bx/math.h>
#include <cmath>
#include <sstream>

namespace Moonlight
{
    namespace
    {
        bool ReadFloats( std::istringstream& InLine, float* OutValues, int InCount )
        {
            for( int i = 0; i < InCount; ++i )
            {
                if( !( InLine >> OutValues[i] ) || !std::isfinite( OutValues[i] ) )
                {
                    return false;
                }
            }
            return true;
        }
    }


    ColorLut::ColorLut( const Path& InPath )
        : Resource( InPath )
    {
    }


    ColorLut::~ColorLut()
    {
        DestroyHandle();
    }


    void ColorLut::DestroyHandle()
    {
        if( bgfx::isValid( m_handle ) && IsGpuAlive() )
        {
            bgfx::destroy( m_handle );
        }
        m_handle = BGFX_INVALID_HANDLE;
    }


    bool ColorLut::Parse( const std::string& InText, int& OutSize, std::vector<float>& OutTexels, std::string& OutError, bool& OutDomainIgnored )
    {
        OutSize = 0;
        OutTexels.clear();
        OutError.clear();
        OutDomainIgnored = false;
        std::istringstream text( InText );
        std::string line;
        size_t expected = 0;
        while( std::getline( text, line ) )
        {
            if( !line.empty() && line.back() == '\r' ) line.pop_back();
            const size_t start = line.find_first_not_of( " \t" );
            if( start == std::string::npos || line[start] == '#' ) continue;
            std::istringstream row( line.substr( start ) );
            // Data rows vastly outnumber keywords; try the three floats first.
            float rgb[3];
            if( ReadFloats( row, rgb, 3 ) )
            {
                if( expected == 0 )
                {
                    OutError = "colour rows before LUT_3D_SIZE";
                    return false;
                }
                if( OutTexels.size() / 4 >= expected )
                {
                    OutError = "more colour rows than LUT_3D_SIZE^3";
                    return false;
                }
                OutTexels.push_back( rgb[0] );
                OutTexels.push_back( rgb[1] );
                OutTexels.push_back( rgb[2] );
                OutTexels.push_back( 1.f );
                continue;
            }
            row.clear();
            row.seekg( 0 );
            std::string keyword;
            row >> keyword;
            if( keyword == "TITLE" ) continue;
            if( keyword == "LUT_3D_SIZE" )
            {
                int size = 0;
                if( !( row >> size ) || size < 2 || size > 256 )
                {
                    OutError = "invalid LUT_3D_SIZE";
                    return false;
                }
                OutSize = size;
                expected = static_cast<size_t>( size ) * size * size;
                OutTexels.reserve( expected * 4 );
                continue;
            }
            if( keyword == "LUT_1D_SIZE" )
            {
                OutError = "1D LUTs aren't supported (use LUT_3D_SIZE)";
                return false;
            }
            if( keyword == "DOMAIN_MIN" || keyword == "DOMAIN_MAX" )
            {
                float domain[3];
                if( !ReadFloats( row, domain, 3 ) )
                {
                    OutError = "invalid " + keyword;
                    return false;
                }
                const float identity = keyword == "DOMAIN_MIN" ? 0.f : 1.f;
                for( float value : domain )
                {
                    if( std::abs( value - identity ) > 1e-4f ) OutDomainIgnored = true;
                }
                continue;
            }
            OutError = "unrecognised line: " + line.substr( start, 40 );
            return false;
        }
        if( expected == 0 )
        {
            OutError = "no LUT_3D_SIZE";
            return false;
        }
        if( OutTexels.size() / 4 != expected )
        {
            OutError = "expected " + std::to_string( expected ) + " colour rows, found " + std::to_string( OutTexels.size() / 4 );
            return false;
        }
        return true;
    }


    bool ColorLut::SupportsAsyncLoad() const
    {
        return true;
    }


    bool ColorLut::LoadAsync()
    {
        std::string text;
        if( !VFS::Get().ReadText( FilePath, text ) || text.empty() )
        {
            YIKES_FMT( "LUT not found: %s", FilePath.GetLocalPathString().c_str() );
            return false;
        }
        int size = 0;
        std::vector<float> texels;
        std::string error;
        bool domainIgnored = false;
        if( !Parse( text, size, texels, error, domainIgnored ) )
        {
            YIKES_FMT( "LUT %s: %s", FilePath.GetLocalPathString().c_str(), error.c_str() );
            return false;
        }
        if( domainIgnored )
        {
            BRUH_FMT( "LUT %s: non-identity DOMAIN_MIN/MAX isn't supported; sampling as 0..1", FilePath.GetLocalPathString().c_str() );
        }
        m_pendingTexels.resize( texels.size() );
        for( size_t i = 0; i < texels.size(); ++i )
        {
            m_pendingTexels[i] = bx::halfFromFloat( texels[i] );
        }
        m_pendingSize = size;
        return true;
    }


    bool ColorLut::FinishAsyncLoad()
    {
        if( m_pendingSize == 0 || m_pendingTexels.empty() )
        {
            return false;
        }
        DestroyHandle();
        const bgfx::Memory* memory = bgfx::copy( m_pendingTexels.data(), static_cast<uint32_t>( m_pendingTexels.size() * sizeof( uint16_t ) ) );
        m_handle = bgfx::createTexture3D( static_cast<uint16_t>( m_pendingSize ), static_cast<uint16_t>( m_pendingSize ), static_cast<uint16_t>( m_pendingSize ),
            false, bgfx::TextureFormat::RGBA16F, BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP | BGFX_SAMPLER_W_CLAMP, memory );
        m_size = m_pendingSize;
        m_pendingTexels.clear();
        m_pendingTexels.shrink_to_fit();
        m_pendingSize = 0;
        return bgfx::isValid( m_handle );
    }


    bool ColorLut::Load()
    {
        return LoadAsync() && FinishAsyncLoad();
    }


    void ColorLut::Reload()
    {
        Load();
    }
}
