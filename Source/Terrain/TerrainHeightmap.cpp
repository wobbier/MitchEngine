#include "PCH.h"
#include "TerrainHeightmap.h"
#include "Utils/GUID.h"
#include <bit>
#include <cmath>
#include <fstream>
#include <limits>
#if USING( ME_PLATFORM_WINDOWS )
#include <Windows.h>
#endif

namespace TerrainData
{
    namespace
    {
        bool Validate( const Settings& settings, const RawHeightmap& format, size_t& size, std::string& error )
        {
            if( !settings.Validate( error ) ) return false;
            if( ( format.Encoding != HeightEncoding::UInt16 && format.Encoding != HeightEncoding::Float32 ) ||
                !std::isfinite( format.Offset ) || !std::isfinite( format.Scale ) || format.Scale == 0 )
            { error = "RAW heightmaps need a supported encoding, finite offset and finite nonzero scale"; return false; }
            const uint64_t width = uint64_t( settings.TilesX ) * settings.TileQuads + 1;
            const uint64_t height = uint64_t( settings.TilesZ ) * settings.TileQuads + 1;
            if( width != format.Width || height != format.Height )
            { error = "RAW dimensions must equal tiles * quads + 1 (including the outer border)"; return false; }
            const uint64_t bpp = format.Encoding == HeightEncoding::UInt16 ? 2 : 4;
            // Bound allocations and multiplication before touching caller-provided file bytes.
            if( width > 64 * 1024 * 1024 / bpp || height > 64 * 1024 * 1024 / ( width * bpp ) )
            { error = "RAW heightmaps are limited to 64 MiB per import/export"; return false; }
            size = static_cast<size_t>( width * height * bpp ); return true;
        }
        uint32_t ReadValue( std::span<const uint8_t> bytes, size_t offset, int count, bool bigEndian )
        {
            uint32_t value = 0;
            for( int i = 0; i < count; ++i ) value |= uint32_t( bytes[offset + i] ) << ( 8 * ( bigEndian ? count - i - 1 : i ) );
            return value;
        }
    }

    bool ImportHeightmap( std::span<const uint8_t> bytes, const Settings& settings, const RawHeightmap& format, Heightfield& out, std::string& error )
    {
        size_t size;
        if( !Validate( settings, format, size, error ) ) return false;
        if( size != bytes.size() ) { error = "RAW byte count does not match the declared dimensions and encoding"; return false; }
        const int bpp = format.Encoding == HeightEncoding::UInt16 ? 2 : 4;
        auto decode = [&]( uint32_t x, uint32_t z, float& result ) {
            const size_t row = format.FlipZ ? format.Height - z - 1 : z;
            const uint32_t bits = ReadValue( bytes, ( row * format.Width + x ) * bpp, bpp, format.BigEndian );
            const double value = format.Encoding == HeightEncoding::UInt16 ? bits / 65535.0 : double( std::bit_cast<float>( bits ) );
            const double height = format.Offset + value * format.Scale;
            if( !std::isfinite( height ) || height < settings.HeightBase || height > double( settings.HeightBase ) + settings.HeightRange ) return false;
            result = static_cast<float>( height ); return true;
        };
        Heightfield field( settings );
        for( uint32_t tz = 0; tz < settings.TilesZ; ++tz ) for( uint32_t tx = 0; tx < settings.TilesX; ++tx )
        {
            std::vector<float> samples( settings.SamplesPerTile() );
            for( uint32_t z = 0; z <= settings.TileQuads; ++z ) for( uint32_t x = 0; x <= settings.TileQuads; ++x )
                if( !decode( tx * settings.TileQuads + x, tz * settings.TileQuads + z, samples[size_t( z ) * ( settings.TileQuads + 1 ) + x] ) )
                { error = "RAW heightmap contains nonfinite heights or elevations outside the terrain range"; return false; }
            if( !field.LoadTile( { settings.MinTile.X + static_cast<int32_t>( tx ), settings.MinTile.Z + static_cast<int32_t>( tz ) }, std::move( samples ), error ) ) return false;
        }
        field.ClearDirty(); out = std::move( field ); error.clear(); return true;
    }

    bool ExportHeightmap( const Heightfield& field, const RawHeightmap& format, std::vector<uint8_t>& out, std::string& error )
    {
        size_t size;
        if( !Validate( field.GetSettings(), format, size, error ) ) return false;
        const auto& settings = field.GetSettings();
        if( field.GetTiles().size() != uint64_t( settings.TilesX ) * settings.TilesZ ) { error = "Load every source page before exporting heights"; return false; }
        std::vector<uint8_t> bytes( size );
        const int bpp = format.Encoding == HeightEncoding::UInt16 ? 2 : 4;
        const auto min = settings.MinSample();
        for( uint32_t z = 0; z < format.Height; ++z ) for( uint32_t x = 0; x < format.Width; ++x )
        {
            float height;
            if( field.GetSample( { min.X + x, min.Z + z }, height ) != QueryStatus::Ready ) { error = "Source sample is unavailable"; return false; }
            const double value = ( height - format.Offset ) / format.Scale;
            uint32_t bits;
            if( format.Encoding == HeightEncoding::UInt16 )
            {
                if( !std::isfinite( value ) || value < 0 || value > 1 ) { error = "Heights do not fit the RAW UInt16 offset/scale range"; return false; }
                bits = static_cast<uint32_t>( std::floor( value * 65535 + 0.5 ) );
            }
            else
            {
                if( !std::isfinite( value ) || std::abs( value ) > std::numeric_limits<float>::max() ) { error = "Heights do not fit the RAW Float32 encoding"; return false; }
                const float encoded = static_cast<float>( value );
                bits = std::bit_cast<uint32_t>( encoded );
            }
            const size_t row = format.FlipZ ? format.Height - z - 1 : z;
            const size_t offset = ( row * format.Width + x ) * bpp;
            for( int i = 0; i < bpp; ++i ) bytes[offset + i] = static_cast<uint8_t>( bits >> ( 8 * ( format.BigEndian ? bpp - i - 1 : i ) ) );
        }
        out = std::move( bytes ); error.clear(); return true;
    }

    bool ReadHeightmap( const std::filesystem::path& path, const Settings& settings, const RawHeightmap& format, Heightfield& out, std::string& error )
    {
        size_t size;
        if( !Validate( settings, format, size, error ) ) return false;
        try
        {
            std::ifstream file( path, std::ios::binary | std::ios::ate );
            if( !file ) { error = "Could not open RAW heightmap"; return false; }
            if( file.tellg() != static_cast<std::streamoff>( size ) ) { error = "RAW file size does not match its dimensions and encoding"; return false; }
            std::vector<uint8_t> bytes( size ); file.seekg( 0 );
            if( !file.read( reinterpret_cast<char*>( bytes.data() ), size ) ) { error = "Could not read complete RAW heightmap"; return false; }
            return ImportHeightmap( bytes, settings, format, out, error );
        }
        catch( const std::exception& e ) { error = e.what(); return false; }
    }

    bool WriteHeightmap( const std::filesystem::path& path, const Heightfield& field, const RawHeightmap& format, std::string& error )
    {
        std::vector<uint8_t> bytes;
        if( !ExportHeightmap( field, format, bytes, error ) ) return false;
        std::filesystem::path temporary;
        try
        {
            if( !path.parent_path().empty() ) std::filesystem::create_directories( path.parent_path() );
            temporary = path; temporary += "." + std::to_string( GUID::Generate() ) + ".tmp";
            std::ofstream file( temporary, std::ios::binary | std::ios::trunc );
            if( !file || !file.write( reinterpret_cast<const char*>( bytes.data() ), bytes.size() ) ) throw std::runtime_error( "Could not write RAW heightmap" );
            file.flush(); if( !file ) throw std::runtime_error( "Could not flush RAW heightmap" );
            file.close(); if( !file ) throw std::runtime_error( "Could not close RAW heightmap" );
#if USING( ME_PLATFORM_WINDOWS )
            if( !MoveFileExW( temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH ) ) throw std::runtime_error( "Could not publish RAW heightmap" );
#else
            std::filesystem::rename( temporary, path );
#endif
            error.clear(); return true;
        }
        catch( const std::exception& e )
        {
            error = e.what(); std::error_code ignored;
            if( !temporary.empty() ) std::filesystem::remove( temporary, ignored );
            return false;
        }
    }
}
