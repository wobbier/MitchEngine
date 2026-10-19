#include "TerrainTexturePacking.h"
#include <bimg/decode.h>
#include <bx/allocator.h>
#include <bx/error.h>
#include <utility>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>

namespace Moonlight
{
    namespace
    {
        using Pixel = std::array<float, 4>;
        struct Image { unsigned Width = 1, Height = 1; std::vector<Pixel> Pixels; };
        float Linear( float c ) { return c <= 0.04045f ? c / 12.92f : std::pow( ( c + 0.055f ) / 1.055f, 2.4f ); }


        Pixel Decode( const uint8_t* bytes, unsigned kind )
        {
            Pixel value{ bytes[0] / 255.f, bytes[1] / 255.f, bytes[2] / 255.f, bytes[3] / 255.f };
            if( kind == 0 )
            {
                static const auto table = [] { std::array<float, 256> t{}; for( unsigned i = 0; i < 256; ++i ) t[i] = Linear( i / 255.f ); return t; }();
                for( unsigned i = 0; i < 3; ++i ) value[i] = table[bytes[i]];
            }
            if( kind == 1 )
            {
                value[0] = value[0] * 2 - 1; value[1] = value[1] * 2 - 1;
                value[2] = std::sqrt( std::max( 0.f, 1 - value[0] * value[0] - value[1] * value[1] ) );
            }
            return value;
        }


        Pixel Normalize( Pixel value, unsigned kind )
        {
            if( kind == 1 )
            {
                const float length = std::sqrt( value[0] * value[0] + value[1] * value[1] + value[2] * value[2] );
                if( length > 1e-6f ) for( unsigned i = 0; i < 3; ++i ) value[i] /= length;
                else value = { 0, 0, 1, 1 };
            }
            return value;
        }


        template<class ReadPixel>
        Image Resize( unsigned width, unsigned height, unsigned side, unsigned kind, ReadPixel read )
        {
            Image result{ side, side, {} }; result.Pixels.resize( size_t( side ) * side );
            const auto footprints = [side]( unsigned extent ) {
                std::vector<std::vector<std::pair<unsigned, float>>> weights( side );
                for( unsigned i = 0; i < side; ++i )
                {
                    if( extent >= side )
                    {
                        const double lo = i * double( extent ) / side, hi = ( i + 1 ) * double( extent ) / side;
                        for( unsigned p = unsigned( lo ); p < std::ceil( hi ); ++p )
                            weights[i].push_back( { std::min( p, extent - 1 ), static_cast<float>( ( std::min( hi, double( p + 1 ) ) - std::max( lo, double( p ) ) ) / ( hi - lo ) ) } );
                    }
                    else
                    {
                        const double point = ( i + 0.5 ) * extent / side - 0.5;
                        const int p = static_cast<int>( std::floor( point ) ); const float fraction = static_cast<float>( point - p );
                        weights[i] = { { unsigned( ( p + int( extent ) ) % int( extent ) ), 1 - fraction }, { unsigned( ( p + 1 + int( extent ) ) % int( extent ) ), fraction } };
                    }
                }
                return weights;
            };
            const auto xWeights = footprints( width ), zWeights = footprints( height );
            for( unsigned z = 0; z < side; ++z ) for( unsigned x = 0; x < side; ++x )
            {
                Pixel sum{};
                for( const auto& [sz, wz] : zWeights[z] ) for( const auto& [sx, wx] : xWeights[x] )
                {
                    const auto pixel = read( sx, sz ); const float weight = wx * wz;
                    for( unsigned c = 0; c < 4; ++c ) sum[c] += pixel[c] * weight;
                }
                result.Pixels[size_t( z ) * side + x] = Normalize( sum, kind );
            }
            return result;
        }


        std::filesystem::path Selected( const std::string& path )
        {
            const std::filesystem::path compiled( path + ".dds" );
            return std::filesystem::is_regular_file( compiled ) ? compiled : std::filesystem::path( path );
        }


        Image Load( const std::string& path, unsigned side, unsigned kind )
        {
            if( path.empty() ) return Image{ side, side, std::vector<Pixel>( size_t( side ) * side, kind == 1 ? Pixel{ 0, 0, 1, 1 } : Pixel{ 1, 1, 1, 1 } ) };
            const auto selected = Selected( path );
            std::ifstream file( selected, std::ios::binary | std::ios::ate );
            if( !file ) throw std::runtime_error( "Cannot read terrain texture: " + selected.string() );
            const auto size = file.tellg();
            if( size <= 0 || size > 128 * 1024 * 1024 ) throw std::runtime_error( "Terrain texture file exceeds 128 MiB: " + selected.string() );
            std::vector<uint8_t> bytes( static_cast<size_t>( size ) ); file.seekg( 0 );
            if( !file.read( reinterpret_cast<char*>( bytes.data() ), size ) ) throw std::runtime_error( "Incomplete terrain texture read" );
            bx::DefaultAllocator allocator; bimg::ImageContainer info{}; bx::Error decodeError;
            if( !bimg::imageParseInfo( &allocator, info, bytes.data(), static_cast<uint32_t>( bytes.size() ), &decodeError ) ||
                !info.m_width || !info.m_height || info.m_width > 4096 || info.m_height > 4096 || info.m_cubeMap || info.m_numLayers != 1 || info.m_depth > 1 )
                throw std::runtime_error( "Terrain textures must be 2D images up to 4096 pixels: " + selected.string() );
            std::unique_ptr<bimg::ImageContainer, decltype( &bimg::imageFree )> image(
                bimg::imageParse( &allocator, bytes.data(), static_cast<uint32_t>( bytes.size() ), bimg::TextureFormat::RGBA8, &decodeError ), bimg::imageFree );
            if( !image ) throw std::runtime_error( "Cannot decode terrain texture: " + selected.string() );
            bimg::ImageMip mip{};
            if( !bimg::imageGetRawData( *image, 0, 0, image->m_data, image->m_size, mip ) ) throw std::runtime_error( "Terrain texture has no base mip" );
            return Resize( mip.m_width, mip.m_height, side, kind, [&]( unsigned x, unsigned z ) { return Decode( static_cast<const uint8_t*>( mip.m_data ) + ( size_t( z ) * mip.m_width + x ) * 4, kind ); } );
        }


        void Store( std::vector<uint8_t>& bytes, size_t offset, Pixel value, unsigned kind )
        {
            for( unsigned c = 0; c < 4; ++c )
            {
                float v = value[c];
                if( kind == 0 && c < 3 ) v = v <= 0.0031308f ? v * 12.92f : 1.055f * std::pow( std::max( v, 0.f ), 1.f / 2.4f ) - 0.055f;
                if( kind == 1 && c < 3 ) v = v * 0.5f + 0.5f;
                bytes[offset + c] = static_cast<uint8_t>( std::clamp( std::round( v * 255 ), 0.f, 255.f ) );
            }
        }
    }


    std::string TerrainTextureStamp( const TerrainTexturePaths& paths )
    {
        std::string result;
        for( const auto& layer : paths ) for( const auto& path : layer )
        {
            result += path + '\n'; if( path.empty() ) continue;
            for( const auto& file : { std::filesystem::path( path ), std::filesystem::path( path + ".dds" ) } )
            {
                std::error_code error; const auto time = std::filesystem::last_write_time( file, error );
                if( error ) result += "missing;";
                else { result += std::to_string( time.time_since_epoch().count() ) + ':'; result += std::to_string( std::filesystem::file_size( file, error ) ) + ';'; }
            }
        }
        return result;
    }


    bool PackTerrainTextures( const TerrainTexturePaths& paths, TerrainTextureLayout layout, TerrainPackedTextures& out, std::string& error, uint16_t cell )
    {
        try
        {
            if( cell < 8 || cell > 512 || ( cell & ( cell - 1 ) ) ) throw std::runtime_error( "Invalid terrain texture packing size" );
            const auto stamp = TerrainTextureStamp( paths );
            TerrainPackedTextures result; result.Layout = layout; result.CellSize = cell;
            uint8_t levels = 0; for( unsigned n = cell; n > 1; n >>= 1 ) ++levels;
            result.AtlasMaxMip = levels - 2;
            for( unsigned kind = 0; kind < 3; ++kind )
            {
                if( layout == TerrainTextureLayout::Atlas ) for( unsigned mip = 0, side = cell * 2; side; ++mip, side >>= 1 )
                    result.Maps[kind].push_back( { 0, uint16_t( side ), uint16_t( side ), uint8_t( mip ), std::vector<uint8_t>( size_t( side ) * side * 4 ) } );
                for( unsigned layer = 0; layer < 4; ++layer )
                {
                    Image image = Load( paths[layer][kind], layout == TerrainTextureLayout::Array ? cell : cell - 4, kind );
                    for( unsigned mip = 0; mip <= ( layout == TerrainTextureLayout::Array ? levels : result.AtlasMaxMip ); ++mip )
                    {
                        const unsigned extent = cell >> mip, gutter = layout == TerrainTextureLayout::Array ? 0 : ( extent >= 8 ? 2 : 1 );
                        const unsigned interior = extent - gutter * 2;
                        if( image.Width != interior ) image = Resize( image.Width, image.Height, interior, kind, [&]( unsigned x, unsigned z ) { return image.Pixels[size_t( z ) * image.Width + x]; } );
                        if( layout == TerrainTextureLayout::Atlas && mip == result.AtlasMaxMip )
                        {
                            Pixel average{}; for( const auto& pixel : image.Pixels ) for( unsigned c = 0; c < 4; ++c ) average[c] += pixel[c] / image.Pixels.size();
                            std::fill( image.Pixels.begin(), image.Pixels.end(), Normalize( average, kind ) );
                        }
                        if( layout == TerrainTextureLayout::Array )
                        {
                            TerrainTextureUpload upload{ uint16_t( layer ), uint16_t( extent ), uint16_t( extent ), uint8_t( mip ), std::vector<uint8_t>( size_t( extent ) * extent * 4 ) };
                            for( size_t i = 0; i < image.Pixels.size(); ++i ) Store( upload.Pixels, i * 4, image.Pixels[i], kind );
                            result.Maps[kind].push_back( std::move( upload ) );
                        }
                        else
                        {
                            auto& upload = result.Maps[kind][mip];
                            for( unsigned z = 0; z < extent; ++z ) for( unsigned x = 0; x < extent; ++x )
                            {
                                const unsigned sx = ( x + interior - gutter ) % interior, sz = ( z + interior - gutter ) % interior;
                                Store( upload.Pixels, ( size_t( ( layer / 2 ) * extent + z ) * upload.Width + ( layer % 2 ) * extent + x ) * 4, image.Pixels[size_t( sz ) * interior + sx], kind );
                            }
                        }
                    }
                }
            }
            if( stamp != TerrainTextureStamp( paths ) ) throw std::runtime_error( "Terrain texture changed during packing; retrying after the next file update" );
            out = std::move( result ); error.clear(); return true;
        }
        catch( const std::exception& e ) { error = e.what(); return false; }
    }
}
