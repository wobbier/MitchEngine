#include "PCH.h"
#include "TerrainSerialization.h"
#include "JSON.h"

#include <algorithm>
#include <bit>
#include <charconv>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace TerrainData
{
    namespace
    {
        constexpr uint32_t kVersion = 1;
        constexpr uint32_t kPageMagic = 0x5048544d; // MTHP, little endian.
        constexpr uint32_t kCookMagic = 0x4e52544d; // MTRN, little endian.
        constexpr size_t kCookHeaderSize = 88;
        constexpr size_t kDirectoryEntrySize = 40;
        constexpr uint64_t kHashOffset = 14695981039346656037ull;

        void Require( bool InCondition, const char* InError )
        {
            if( !InCondition ) throw std::runtime_error( InError );
        }

        uint64_t Hash( std::span<const uint8_t> InBytes, uint64_t InSeed = kHashOffset )
        {
            for( uint8_t byte : InBytes ) InSeed = ( InSeed ^ byte ) * 1099511628211ull;
            return InSeed;
        }

        struct Writer
        {
            std::vector<uint8_t> Data;
            void U16( uint16_t v ) { for( unsigned i = 0; i < 2; ++i ) Data.push_back( static_cast<uint8_t>( v >> ( 8 * i ) ) ); }
            void U32( uint32_t v ) { for( unsigned i = 0; i < 4; ++i ) Data.push_back( static_cast<uint8_t>( v >> ( 8 * i ) ) ); }
            void U64( uint64_t v ) { for( unsigned i = 0; i < 8; ++i ) Data.push_back( static_cast<uint8_t>( v >> ( 8 * i ) ) ); }
            void I32( int32_t v ) { U32( std::bit_cast<uint32_t>( v ) ); }
            void F32( float v ) { U32( std::bit_cast<uint32_t>( v ) ); }
            void Bytes( std::span<const uint8_t> bytes ) { Data.insert( Data.end(), bytes.begin(), bytes.end() ); }
        };

        struct Reader
        {
            std::span<const uint8_t> Data;
            size_t Position = 0;
            std::span<const uint8_t> Bytes( size_t n )
            {
                Require( n <= Data.size() - Position, "Truncated terrain data" );
                auto result = Data.subspan( Position, n ); Position += n; return result;
            }
            uint16_t U16() { auto b = Bytes( 2 ); return static_cast<uint16_t>( b[0] | ( uint16_t( b[1] ) << 8 ) ); }
            uint32_t U32() { auto b = Bytes( 4 ); uint32_t v = 0; for( unsigned i = 0; i < 4; ++i ) v |= uint32_t( b[i] ) << ( 8 * i ); return v; }
            uint64_t U64() { auto b = Bytes( 8 ); uint64_t v = 0; for( unsigned i = 0; i < 8; ++i ) v |= uint64_t( b[i] ) << ( 8 * i ); return v; }
            int32_t I32() { return std::bit_cast<int32_t>( U32() ); }
            float F32() { return std::bit_cast<float>( U32() ); }
        };

        void WriteSettings( Writer& w, const Settings& s )
        {
            w.U32( s.TileQuads ); w.U32( s.PatchQuads ); w.F32( s.SampleSpacing ); w.F32( s.HeightBase ); w.F32( s.HeightRange );
            w.I32( s.MinTile.X ); w.I32( s.MinTile.Z ); w.U32( s.TilesX ); w.U32( s.TilesZ );
        }

        Settings ReadSettings( Reader& r )
        {
            Settings s;
            s.TileQuads = r.U32(); s.PatchQuads = r.U32(); s.SampleSpacing = r.F32(); s.HeightBase = r.F32(); s.HeightRange = r.F32();
            s.MinTile.X = r.I32(); s.MinTile.Z = r.I32(); s.TilesX = r.U32(); s.TilesZ = r.U32();
            std::string error;
            if( !s.Validate( error ) ) throw std::runtime_error( error );
            return s;
        }

        TileCoord ExpectedTile( const Settings& s, uint64_t i )
        {
            return { static_cast<int32_t>( static_cast<int64_t>( s.MinTile.X ) + static_cast<int64_t>( i / s.TilesZ ) ), static_cast<int32_t>( static_cast<int64_t>( s.MinTile.Z ) + static_cast<int64_t>( i % s.TilesZ ) ) };
        }

        bool SafeRelativePath( const std::string& path )
        {
            if( path.empty() || path.front() == '/' || path.find_first_of( "\\:" ) != std::string::npos || path.find( '\0' ) != std::string::npos ) return false;
            size_t start = 0;
            while( start <= path.size() )
            {
                const size_t end = path.find( '/', start );
                const auto part = path.substr( start, end == std::string::npos ? end : end - start );
                if( part.empty() || part == "." || part == ".." ) return false;
                if( end == std::string::npos ) break;
                start = end + 1;
            }
            return true;
        }

        void ValidateManifest( const Manifest& m )
        {
            std::string error;
            if( !m.Config.Validate( error ) ) throw std::runtime_error( error );
            Require( m.AssetGUID != 0, "Terrain manifest requires a nonzero asset GUID" );
            Require( m.Pages.size() == static_cast<uint64_t>( m.Config.TilesX ) * m.Config.TilesZ, "Terrain manifest must reference every tile exactly once" );
            Require( m.SurfaceSource.empty() || SafeRelativePath( m.SurfaceSource ), "Terrain surface path must be a portable relative path" );
            std::set<TileCoord> coords;
            std::set<std::string> paths;
            for( const auto& page : m.Pages )
            {
                Require( m.Config.Contains( page.Coordinate ) && coords.insert( page.Coordinate ).second, "Invalid or duplicate terrain page coordinates" );
                Require( SafeRelativePath( page.Source ) && paths.insert( page.Source ).second, "Terrain page paths must be unique portable relative paths" );
            }
        }

        int64_t Integer( const json& j, int64_t min, int64_t max )
        {
            Require( j.is_number_integer(), "Terrain manifest requires an integer" );
            if( j.is_number_unsigned() )
            {
                const uint64_t value = j.get<uint64_t>();
                Require( value <= static_cast<uint64_t>( max ) && ( min <= 0 || value >= static_cast<uint64_t>( min ) ), "Terrain manifest integer is out of range" );
                return static_cast<int64_t>( value );
            }
            const int64_t value = j.get<int64_t>();
            Require( value >= min && value <= max, "Terrain manifest integer is out of range" );
            return value;
        }

        float Number( const json& j )
        {
            Require( j.is_number(), "Terrain manifest requires a number" );
            const double value = j.get<double>();
            Require( std::isfinite( value ) && std::abs( value ) <= std::numeric_limits<float>::max(), "Terrain manifest number is out of range" );
            return static_cast<float>( value );
        }

        float Decode( uint16_t v, const Settings& s )
        {
            return static_cast<float>( s.HeightBase + static_cast<double>( v ) * s.HeightRange / 65535.0 );
        }

        template<typename Fn>
        bool Checked( std::string& error, Fn&& fn )
        {
            error.clear();
            try { fn(); return true; }
            catch( const std::exception& e ) { error = e.what(); return false; }
        }
    }

    bool SerializeManifest( const Manifest& InManifest, std::string& OutText, std::string& OutError )
    {
        return Checked( OutError, [&] {
            ValidateManifest( InManifest );
            const auto& s = InManifest.Config;
            std::ostringstream guid; guid << std::hex << std::setfill( '0' ) << std::setw( 16 ) << InManifest.AssetGUID;
            json j = { { "Format", "MitchTerrain" }, { "Version", kVersion }, { "AssetGUID", guid.str() },
                { "Settings", { { "TileQuads", s.TileQuads }, { "PatchQuads", s.PatchQuads }, { "SampleSpacing", s.SampleSpacing },
                    { "HeightBase", s.HeightBase }, { "HeightRange", s.HeightRange }, { "MinTile", { s.MinTile.X, s.MinTile.Z } }, { "TilesX", s.TilesX }, { "TilesZ", s.TilesZ } } },
                { "Pages", json::array() } };
            auto pages = InManifest.Pages;
            std::sort( pages.begin(), pages.end(), []( const auto& a, const auto& b ) { return a.Coordinate < b.Coordinate; } );
            for( const auto& page : pages ) j["Pages"].push_back( { { "Tile", { page.Coordinate.X, page.Coordinate.Z } }, { "Source", page.Source } } );
            if( !InManifest.SurfaceSource.empty() ) { j["Version"] = 2; j["Surface"] = InManifest.SurfaceSource; }
            OutText = j.dump( 2 ) + "\n";
        } );
    }

    bool DeserializeManifest( std::string_view InText, Manifest& OutManifest, std::string& OutError )
    {
        return Checked( OutError, [&] {
            const json j = json::parse( InText );
            Require( j.at( "Format" ) == "MitchTerrain" && Integer( j.at( "Version" ), 1, UINT32_MAX ) <= 2, "Unsupported terrain manifest format/version" );
            Manifest m;
            const auto guid = j.at( "AssetGUID" ).get<std::string>();
            Require( guid.size() == 16, "Terrain GUID must contain sixteen hexadecimal digits" );
            const auto parsed = std::from_chars( guid.data(), guid.data() + guid.size(), m.AssetGUID, 16 );
            Require( parsed.ec == std::errc() && parsed.ptr == guid.data() + guid.size(), "Invalid terrain asset GUID" );
            const auto& s = j.at( "Settings" );
            m.Config.TileQuads = static_cast<uint32_t>( Integer( s.at( "TileQuads" ), 1, 4096 ) );
            m.Config.PatchQuads = static_cast<uint32_t>( Integer( s.at( "PatchQuads" ), 1, 1024 ) );
            m.Config.SampleSpacing = Number( s.at( "SampleSpacing" ) ); m.Config.HeightBase = Number( s.at( "HeightBase" ) ); m.Config.HeightRange = Number( s.at( "HeightRange" ) );
            const auto& min = s.at( "MinTile" ); Require( min.is_array() && min.size() == 2, "Terrain MinTile must be [x,z]" );
            m.Config.MinTile = { static_cast<int32_t>( Integer( min[0], INT32_MIN, INT32_MAX ) ), static_cast<int32_t>( Integer( min[1], INT32_MIN, INT32_MAX ) ) };
            m.Config.TilesX = static_cast<uint32_t>( Integer( s.at( "TilesX" ), 1, UINT32_MAX ) ); m.Config.TilesZ = static_cast<uint32_t>( Integer( s.at( "TilesZ" ), 1, UINT32_MAX ) );
            const auto& pages = j.at( "Pages" ); Require( pages.is_array(), "Terrain Pages must be an array" );
            Require( pages.size() == static_cast<uint64_t>( m.Config.TilesX ) * m.Config.TilesZ, "Terrain manifest page count does not match its extent" );
            for( const auto& p : pages )
            {
                const auto& coord = p.at( "Tile" ); Require( coord.is_array() && coord.size() == 2, "Terrain Tile must be [x,z]" );
                m.Pages.push_back( { { static_cast<int32_t>( Integer( coord[0], INT32_MIN, INT32_MAX ) ), static_cast<int32_t>( Integer( coord[1], INT32_MIN, INT32_MAX ) ) }, p.at( "Source" ).get<std::string>() } );
            }
            if( j.at( "Version" ) == 2 )
            {
                m.SurfaceSource = j.at( "Surface" ).get<std::string>();
                Require( !m.SurfaceSource.empty(), "Version 2 terrain requires a surface companion" );
            }
            ValidateManifest( m );
            OutManifest = std::move( m );
        } );
    }

    bool SerializePage( const Heightfield& InField, TileCoord InTile, std::vector<uint8_t>& OutData, std::string& OutError )
    {
        return Checked( OutError, [&] {
            const Tile* tile = InField.FindTile( InTile ); Require( tile != nullptr, "Terrain source page is not resident" );
            Writer payload;
            for( float h : tile->Heights ) payload.F32( h );
            Writer w;
            w.U32( kPageMagic ); w.U32( kVersion ); w.U32( InField.GetSettings().TileQuads ); w.I32( InTile.X ); w.I32( InTile.Z );
            w.U64( payload.Data.size() ); w.U64( Hash( payload.Data ) ); w.Bytes( payload.Data );
            OutData = std::move( w.Data );
        } );
    }

    bool DeserializePage( std::span<const uint8_t> InData, const Settings& InSettings, TilePage& OutPage, std::string& OutError )
    {
        return Checked( OutError, [&] {
            if( !InSettings.Validate( OutError ) ) throw std::runtime_error( OutError );
            Reader r{ InData };
            Require( r.U32() == kPageMagic && r.U32() == kVersion, "Unsupported terrain page format/version" );
            Require( r.U32() == InSettings.TileQuads, "Terrain page dimensions differ from the manifest" );
            TilePage page; page.Coordinate.X = r.I32(); page.Coordinate.Z = r.I32();
            Require( InSettings.Contains( page.Coordinate ), "Terrain page coordinates are outside the manifest" );
            const uint64_t size = r.U64(), checksum = r.U64();
            Require( size == InSettings.SamplesPerTile() * 4 && size == InData.size() - r.Position, "Terrain page payload length is invalid" );
            const auto payload = r.Bytes( static_cast<size_t>( size ) );
            Require( Hash( payload ) == checksum, "Terrain page checksum failed" );
            Reader samples{ payload };
            page.Heights.reserve( InSettings.SamplesPerTile() );
            for( size_t i = 0; i < InSettings.SamplesPerTile(); ++i )
            {
                const float h = samples.F32(); Require( std::isfinite( h ), "Terrain page contains a non-finite height" ); page.Heights.push_back( h );
            }
            OutPage = std::move( page );
        } );
    }

    bool CookHeightfield( const Heightfield& InField, uint64_t InAssetGUID, HeightEncoding InEncoding, std::vector<uint8_t>& OutData, std::string& OutError )
    {
        return Checked( OutError, [&] {
            Require( !InField.GetSurface(), "This cooked format does not support terrain surface layers; use the .terrain source asset" );
            const Settings& s = InField.GetSettings();
            Require( InAssetGUID != 0, "Cooked terrain requires a nonzero asset GUID" );
            Require( InEncoding == HeightEncoding::Float32 || InEncoding == HeightEncoding::UInt16, "Unsupported terrain height encoding" );
            const uint64_t count = static_cast<uint64_t>( s.TilesX ) * s.TilesZ;
            Require( InField.GetTiles().size() == count, "Load every source tile before cooking the terrain" );
            const size_t payloadSize = s.SamplesPerTile() * ( InEncoding == HeightEncoding::UInt16 ? 2 : 4 );
            Require( count <= ( std::numeric_limits<size_t>::max() - kCookHeaderSize ) / ( kDirectoryEntrySize + payloadSize ), "Cooked terrain is too large" );
            uint64_t offset = kCookHeaderSize + count * kDirectoryEntrySize;
            Writer directory, payloads, sourceSettings;
            sourceSettings.U32( kVersion ); sourceSettings.U32( static_cast<uint32_t>( InEncoding ) ); WriteSettings( sourceSettings, s );
            uint64_t sourceHash = Hash( sourceSettings.Data );
            for( const auto& [coord, tile] : InField.GetTiles() )
            {
                Writer payload, source;
                source.I32( coord.X ); source.I32( coord.Z );
                float min = std::numeric_limits<float>::max(), max = -std::numeric_limits<float>::max();
                for( float h : tile.Heights )
                {
                    source.F32( h );
                    float decoded = h;
                    if( InEncoding == HeightEncoding::UInt16 )
                    {
                        Require( h >= static_cast<double>( s.HeightBase ) && h <= static_cast<double>( s.HeightBase ) + s.HeightRange, "Terrain height exceeds the UInt16 encoding range" );
                        const auto q = static_cast<uint16_t>( std::llround( ( static_cast<double>( h ) - s.HeightBase ) * 65535.0 / s.HeightRange ) );
                        payload.U16( q ); decoded = Decode( q, s );
                    }
                    else payload.F32( h );
                    min = std::min( min, decoded ); max = std::max( max, decoded );
                }
                sourceHash = Hash( source.Data, sourceHash );
                directory.I32( coord.X ); directory.I32( coord.Z ); directory.U64( offset ); directory.U64( payload.Data.size() ); directory.U64( Hash( payload.Data ) ); directory.F32( min ); directory.F32( max );
                payloads.Bytes( payload.Data ); offset += payload.Data.size();
            }
            Writer w;
            w.U32( kCookMagic ); w.U32( kVersion ); w.U32( static_cast<uint32_t>( InEncoding ) ); WriteSettings( w, s );
            w.U64( InAssetGUID ); w.U64( count ); w.U64( sourceHash ); w.U64( Hash( directory.Data ) ); w.U64( Hash( w.Data ) );
            Require( w.Data.size() == kCookHeaderSize, "Internal terrain header size mismatch" );
            w.Bytes( directory.Data ); w.Bytes( payloads.Data );
            OutData = std::move( w.Data );
        } );
    }

    bool ReadCookedHeightfield( std::span<const uint8_t> InData, Heightfield& OutField, CookInfo& OutInfo, std::string& OutError )
    {
        return Checked( OutError, [&] {
            Reader r{ InData };
            Require( r.U32() == kCookMagic && r.U32() == kVersion, "Unsupported cooked terrain format/version" );
            CookInfo info; info.Encoding = static_cast<HeightEncoding>( r.U32() );
            Require( info.Encoding == HeightEncoding::Float32 || info.Encoding == HeightEncoding::UInt16, "Unsupported terrain height encoding" );
            const Settings s = ReadSettings( r );
            info.AssetGUID = r.U64(); const uint64_t count = r.U64(); info.SourceHash = r.U64(); const uint64_t directoryHash = r.U64(), headerHash = r.U64();
            Require( Hash( InData.first( kCookHeaderSize - 8 ) ) == headerHash, "Cooked terrain header checksum failed" );
            Require( info.AssetGUID != 0 && count == static_cast<uint64_t>( s.TilesX ) * s.TilesZ, "Cooked terrain tile count or GUID is invalid" );
            Require( count <= ( InData.size() - r.Position ) / kDirectoryEntrySize, "Truncated cooked terrain directory" );
            const auto directory = r.Bytes( static_cast<size_t>( count ) * kDirectoryEntrySize );
            Require( Hash( directory ) == directoryHash, "Cooked terrain directory checksum failed" );
            Reader entries{ directory };
            Heightfield field( s );
            uint64_t expectedOffset = r.Position;
            const size_t payloadSize = s.SamplesPerTile() * ( info.Encoding == HeightEncoding::UInt16 ? 2 : 4 );
            for( uint64_t i = 0; i < count; ++i )
            {
                TileCoord coord; coord.X = entries.I32(); coord.Z = entries.I32();
                const uint64_t offset = entries.U64(), size = entries.U64(), checksum = entries.U64();
                const float expectedMin = entries.F32(), expectedMax = entries.F32();
                Require( coord == ExpectedTile( s, i ), "Cooked terrain directory tiles are invalid or unordered" );
                Require( offset == expectedOffset && size == payloadSize && offset <= InData.size() && size <= InData.size() - offset, "Cooked terrain block offset/size is invalid" );
                const auto bytes = InData.subspan( static_cast<size_t>( offset ), static_cast<size_t>( size ) );
                Require( Hash( bytes ) == checksum, "Cooked terrain tile checksum failed" );
                Reader samples{ bytes };
                std::vector<float> heights; heights.reserve( s.SamplesPerTile() );
                float min = std::numeric_limits<float>::max(), max = -std::numeric_limits<float>::max();
                for( size_t n = 0; n < s.SamplesPerTile(); ++n )
                {
                    const float h = info.Encoding == HeightEncoding::UInt16 ? Decode( samples.U16(), s ) : samples.F32();
                    Require( std::isfinite( h ), "Cooked terrain contains a non-finite height" );
                    min = std::min( min, h ); max = std::max( max, h ); heights.push_back( h );
                }
                Require( min == expectedMin && max == expectedMax, "Cooked terrain tile bounds disagree with its samples" );
                std::string error;
                if( !field.LoadTile( coord, std::move( heights ), error ) ) throw std::runtime_error( error );
                expectedOffset += size;
            }
            Require( expectedOffset == InData.size(), "Cooked terrain contains trailing data" );
            if( info.Encoding == HeightEncoding::UInt16 )
            {
                const double magnitude = std::max( std::abs( static_cast<double>( s.HeightBase ) ), std::abs( static_cast<double>( s.HeightBase ) + s.HeightRange ) );
                info.QuantizationErrorBound = s.HeightRange / ( 2.0 * 65535.0 ) + std::max( magnitude * std::numeric_limits<float>::epsilon(), static_cast<double>( std::numeric_limits<float>::denorm_min() ) );
            }
            field.ClearDirty();
            OutField = std::move( field ); OutInfo = info;
        } );
    }
}
