#include "PCH.h"
#include "TerrainStreamArchive.h"
#include "Utils/GUID.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <fstream>
#include <limits>
#include <stdexcept>
#if USING( ME_PLATFORM_WINDOWS )
#include <Windows.h>
#endif

namespace TerrainData
{
    namespace
    {
        constexpr uint32_t kMagic = 0x3253544d; // MTS2, little endian.
        constexpr uint32_t kPagedMagic = 0x3353544d; // MTS3.
        constexpr size_t kHeaderBytes = 104, kEntryBytes = 40, kPagedEntryBytes = 64;
        constexpr uint64_t kCoverageLimit = 64ull << 20, kTileLimit = 65536, kPatchLimit = 65536;
        constexpr uint64_t kHashSeed = 14695981039346656037ull;

        void Require( bool condition, const char* message ) { if( !condition ) throw std::runtime_error( message ); }
        uint64_t Hash( std::span<const uint8_t> bytes, uint64_t seed = kHashSeed )
        {
            for( uint8_t value : bytes ) seed = ( seed ^ value ) * 1099511628211ull;
            return seed;
        }

        struct Bytes
        {
            std::vector<uint8_t> Data;
            size_t Position = 0;
            void U32( uint32_t v ) { for( int i = 0; i < 4; ++i ) Data.push_back( static_cast<uint8_t>( v >> ( 8 * i ) ) ); }
            void U64( uint64_t v ) { for( int i = 0; i < 8; ++i ) Data.push_back( static_cast<uint8_t>( v >> ( 8 * i ) ) ); }
            void F32( float v ) { U32( std::bit_cast<uint32_t>( v ) ); }
            uint32_t Read32()
            {
                Require( Position <= Data.size() && Data.size() - Position >= 4, "Truncated terrain stream data" );
                uint32_t v = 0; for( int i = 0; i < 4; ++i ) v |= uint32_t( Data[Position++] ) << ( 8 * i ); return v;
            }
            uint64_t Read64() { const uint64_t lo = Read32(); return lo | ( uint64_t( Read32() ) << 32 ); }
            float ReadFloat() { const float v = std::bit_cast<float>( Read32() ); Require( std::isfinite( v ), "Nonfinite terrain stream value" ); return v; }
        };

        void SettingsBytes( Bytes& w, const Settings& s )
        {
            w.U32( s.TileQuads ); w.U32( s.PatchQuads ); w.F32( s.SampleSpacing ); w.F32( s.HeightBase ); w.F32( s.HeightRange );
            w.U32( std::bit_cast<uint32_t>( s.MinTile.X ) ); w.U32( std::bit_cast<uint32_t>( s.MinTile.Z ) ); w.U32( s.TilesX ); w.U32( s.TilesZ );
        }

        Settings SettingsBytes( Bytes& r )
        {
            Settings s; s.TileQuads = r.Read32(); s.PatchQuads = r.Read32(); s.SampleSpacing = r.ReadFloat(); s.HeightBase = r.ReadFloat(); s.HeightRange = r.ReadFloat();
            s.MinTile.X = std::bit_cast<int32_t>( r.Read32() ); s.MinTile.Z = std::bit_cast<int32_t>( r.Read32() ); s.TilesX = r.Read32(); s.TilesZ = r.Read32();
            std::string error; if( !s.Validate( error ) ) throw std::runtime_error( error );
            Require( s.TileQuads <= 256, "Streamed tiles are limited to 256 quads; retile before cooking" );
            for( int64_t sample : { s.MinSample().X, s.MinSample().Z, s.MaxSample().X, s.MaxSample().Z } )
            {
                const float position = static_cast<float>( sample * double( s.SampleSpacing ) );
                Require( std::isfinite( position ) && position + s.SampleSpacing != position, "Terrain stream coordinates lose canonical Float32 precision" );
            }
            return s;
        }

        uint32_t LastLevel( uint32_t q ) { return std::bit_width( q ) - 1; }
        uint64_t PatchCount( const Settings& s ) { const uint64_t side = s.TileQuads / s.PatchQuads; return uint64_t( s.TilesX ) * s.TilesZ * side * side; }
        TileCoord Coordinate( const Settings& s, size_t i )
        {
            return { static_cast<int32_t>( int64_t( s.MinTile.X ) + i / s.TilesZ ), static_cast<int32_t>( int64_t( s.MinTile.Z ) + i % s.TilesZ ) };
        }

        Bytes Read( std::ifstream& file, uint64_t offset, uint64_t size )
        {
            Require( size <= kCoverageLimit, "Terrain stream read exceeds the bounded block limit" );
            Bytes bytes; bytes.Data.resize( static_cast<size_t>( size ) );
            file.seekg( static_cast<std::streamoff>( offset ) );
            Require( file.good() && ( !size || bool( file.read( reinterpret_cast<char*>( bytes.Data.data() ), static_cast<std::streamsize>( size ) ) ) ), "Could not read terrain stream block" );
            return bytes;
        }

        void Write( std::ofstream& file, uint64_t offset, const Bytes& bytes )
        {
            file.seekp( static_cast<std::streamoff>( offset ) );
            file.write( reinterpret_cast<const char*>( bytes.Data.data() ), static_cast<std::streamsize>( bytes.Data.size() ) );
            Require( file.good(), "Could not write terrain stream block" );
        }

        std::shared_ptr<ResidentPatch> MakePatch( const Settings& s, TileCoord tile, uint32_t px, uint32_t pz, uint8_t base )
        {
            auto patch = std::make_shared<ResidentPatch>(); patch->BaseLod = base;
            patch->Mesh.SampleOrigin = { int64_t( tile.X ) * s.TileQuads + px * s.PatchQuads, int64_t( tile.Z ) * s.TileQuads + pz * s.PatchQuads };
            patch->Offset = Vector3( static_cast<float>( patch->Mesh.SampleOrigin.X * double( s.SampleSpacing ) ), 0.f, static_cast<float>( patch->Mesh.SampleOrigin.Z * double( s.SampleSpacing ) ) );
            Require( std::isfinite( patch->Offset.x ) && std::isfinite( patch->Offset.z ), "Terrain stream coordinates exceed Float32" );
            for( float p : { patch->Offset.x, patch->Offset.z } ) Require( p + s.SampleSpacing != p, "Terrain stream coordinates lose canonical sample precision" );
            return patch;
        }

        Vector3 ReadNormal( Bytes& bytes )
        {
            const float x = bytes.ReadFloat(), y = bytes.ReadFloat(), z = bytes.ReadFloat();
            const double length = std::hypot( std::hypot( x, y ), z );
            Require( std::abs( length - 1.0 ) < 1e-4 && y >= 0, "Invalid canonical terrain normal" ); return Vector3( x, y, z );
        }
    }


    size_t StreamArchive::TileIndex( TileCoord tile ) const
    {
        if( !Config.Contains( tile ) ) throw std::out_of_range( "Terrain stream tile is outside the archive" );
        return static_cast<size_t>( int64_t( tile.X ) - Config.MinTile.X ) * Config.TilesZ + static_cast<size_t>( int64_t( tile.Z ) - Config.MinTile.Z );
    }


    bool IsStreamArchive( std::span<const uint8_t> prefix )
    {
        return prefix.size() >= 4 && prefix[0] == 'M' && prefix[1] == 'T' && prefix[2] == 'S' && ( prefix[3] == '2' || prefix[3] == '3' );
    }


    void BuildPatchNeighbors( ResidentTerrain& terrain )
    {
        std::map<std::pair<int64_t, int64_t>, int32_t> addresses;
        for( size_t i = 0; i < terrain.Patches.size(); ++i )
        { const auto s = terrain.Patches[i]->Mesh.SampleOrigin; addresses[{ s.X, s.Z }] = static_cast<int32_t>( i ); }
        const int64_t q = terrain.Heights->GetSettings().PatchQuads;
        const SampleCoord directions[] = { { -q, 0 }, { q, 0 }, { 0, -q }, { 0, q } };
        terrain.PatchNeighbors.clear(); terrain.PatchNeighbors.reserve( terrain.Patches.size() );
        for( const auto& patch : terrain.Patches )
        {
            std::array<int32_t, 4> neighbors{ -1, -1, -1, -1 };
            for( size_t edge = 0; edge < 4; ++edge )
            {
                const auto s = patch->Mesh.SampleOrigin;
                if( auto found = addresses.find( { s.X + directions[edge].X, s.Z + directions[edge].Z } ); found != addresses.end() ) neighbors[edge] = found->second;
            }
            terrain.PatchNeighbors.push_back( neighbors );
        }
    }


    static bool CookArchive( const std::filesystem::path& path, const Heightfield& field, uint64_t guid, uint8_t base, bool paged, std::string& error )
    {
        std::filesystem::path temporary;
        try
        {
            Require( !field.GetSurface(), "Streaming archives do not yet support terrain surface layers; use the .terrain source asset" );
            const auto& s = field.GetSettings(); const uint64_t count = uint64_t( s.TilesX ) * s.TilesZ;
            Require( path.extension() == ".terrainc" && guid && count <= kTileLimit && field.GetTiles().size() == count, "Stream cooking requires a .terrainc destination, GUID and every source tile" );
            Require( s.TileQuads <= 256 && base <= LastLevel( s.PatchQuads ) && ( paged || PatchCount( s ) <= kPatchLimit ), "Stream cooking requires tiles <= 256 quads and bounded patch coverage" );
            for( int64_t sample : { s.MinSample().X, s.MinSample().Z, s.MaxSample().X, s.MaxSample().Z } )
            {
                const float position = static_cast<float>( sample * double( s.SampleSpacing ) );
                Require( std::isfinite( position ) && position + s.SampleSpacing != position, "Terrain stream coordinates lose canonical Float32 precision" );
            }
            const uint32_t q = s.PatchQuads >> base, levels = LastLevel( q ) + 1, patches = s.TileQuads / s.PatchQuads;
            const uint64_t coverageSize = PatchCount( s ) * ( 8 + levels * 4 + uint64_t( q + 1 ) * ( q + 1 ) * 16 );
            Require( ( paged ? coverageSize / count : coverageSize ) <= kCoverageLimit, "Terrain coarse coverage exceeds 64 MiB; increase its stride or split the dataset" );
            if( !path.parent_path().empty() ) std::filesystem::create_directories( path.parent_path() );
            temporary = path; temporary += "." + std::to_string( GUID::Generate() ) + ".tmp";
            std::ofstream file( temporary, std::ios::binary | std::ios::trunc ); Require( bool( file ), "Could not stage terrain cook" );
            Bytes coverage, directory; coverage.Data.reserve( static_cast<size_t>( paged ? coverageSize / count : coverageSize ) );
            uint64_t offset = kHeaderBytes + count * ( paged ? kPagedEntryBytes : kEntryBytes ) + ( paged ? 0 : coverageSize ), revision = kHashSeed;
            for( const auto& [tile, source] : field.GetTiles() )
            {
                Bytes payload; payload.Data.reserve( s.SamplesPerTile() * 16 );
                float min = std::numeric_limits<float>::max(), max = -min;
                for( uint32_t z = 0; z <= s.TileQuads; ++z ) for( uint32_t x = 0; x <= s.TileQuads; ++x )
                {
                    const float h = source.Heights[size_t( z ) * ( s.TileQuads + 1 ) + x]; Vector3 n;
                    Require( field.GetNormal( { int64_t( tile.X ) * s.TileQuads + x, int64_t( tile.Z ) * s.TileQuads + z }, n ) == QueryStatus::Ready, "Canonical normal halo is missing" );
                    payload.F32( h ); payload.F32( n.x ); payload.F32( n.y ); payload.F32( n.z ); min = std::min( min, h ); max = std::max( max, h );
                }
                for( uint32_t z = 0; z < patches; ++z ) for( uint32_t x = 0; x < patches; ++x )
                {
                    PatchMesh mesh; if( !BuildPatch( field, tile, x, z, mesh, error ) ) throw std::runtime_error( error );
                    coverage.F32( mesh.Bounds.Min.y ); coverage.F32( mesh.Bounds.Max.y );
                    for( size_t l = base; l < mesh.Lods.size(); ++l ) coverage.F32( mesh.Lods[l].MaxError );
                    for( uint32_t vz = 0; vz <= s.PatchQuads; vz += 1u << base ) for( uint32_t vx = 0; vx <= s.PatchQuads; vx += 1u << base )
                    {
                        const auto& v = mesh.Vertices[size_t( vz ) * ( s.PatchQuads + 1 ) + vx];
                        coverage.F32( v.Position.y ); coverage.F32( v.Normal.x ); coverage.F32( v.Normal.y ); coverage.F32( v.Normal.z );
                    }
                }
                const uint64_t coverageOffset = paged ? offset : 0;
                const uint64_t coverageHash = paged ? Hash( coverage.Data ) : 0;
                if( paged ) { Write( file, offset, coverage ); offset += coverage.Data.size(); }
                directory.U32( std::bit_cast<uint32_t>( tile.X ) ); directory.U32( std::bit_cast<uint32_t>( tile.Z ) );
                directory.U64( offset ); directory.U64( payload.Data.size() ); directory.U64( Hash( payload.Data ) ); directory.F32( min ); directory.F32( max );
                if( paged ) { directory.U64( coverageOffset ); directory.U64( coverage.Data.size() ); directory.U64( coverageHash ); coverage.Data.clear(); }
                Write( file, offset, payload ); offset += payload.Data.size(); revision = Hash( payload.Data, revision );
            }
            Require( paged || coverage.Data.size() == coverageSize, "Internal terrain coverage size mismatch" );
            Bytes header; header.U32( paged ? kPagedMagic : kMagic ); header.U32( paged ? 3 : 2 ); SettingsBytes( header, s ); header.U32( base ); header.U64( guid ); header.U64( revision );
            header.U64( count ); header.U64( paged ? 0 : coverageSize ); header.U64( paged ? 0 : Hash( coverage.Data ) ); header.U64( Hash( directory.Data ) ); header.U64( Hash( header.Data ) );
            Require( header.Data.size() == kHeaderBytes, "Internal terrain stream header mismatch" );
            Write( file, 0, header ); Write( file, kHeaderBytes, directory ); if( !paged ) Write( file, kHeaderBytes + directory.Data.size(), coverage );
            file.flush(); Require( file.good(), "Could not flush terrain stream cook" ); file.close(); Require( !file.fail(), "Could not close terrain stream cook" );
#if USING( ME_PLATFORM_WINDOWS )
            Require( MoveFileExW( temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH ) != 0, "Could not publish terrain stream cook" );
#else
            std::filesystem::rename( temporary, path );
#endif
            error.clear(); return true;
        }
        catch( const std::exception& e )
        { if( !temporary.empty() ) { std::error_code ignored; std::filesystem::remove( temporary, ignored ); } error = e.what(); return false; }
    }


    bool CookStreamArchive( const std::filesystem::path& path, const Heightfield& field, uint64_t guid, uint8_t base, std::string& error )
    {
        return CookArchive( path, field, guid, base, false, error );
    }


    bool CookPagedStreamArchive( const std::filesystem::path& path, const Heightfield& field, uint64_t guid, uint8_t base, std::string& error )
    {
        return CookArchive( path, field, guid, base, true, error );
    }


    bool OpenStreamArchive( const std::filesystem::path& path, std::shared_ptr<const StreamArchive>& out, std::string& error )
    {
        try
        {
            std::ifstream file( path, std::ios::binary | std::ios::ate ); Require( bool( file ), "Could not open terrain stream archive" );
            const auto size = file.tellg(); Require( size >= static_cast<std::streamoff>( kHeaderBytes ), "Truncated terrain stream header" );
            auto archive = std::make_shared<StreamArchive>(); archive->Path = path; archive->FileSize = static_cast<uint64_t>( size );
            auto header = Read( file, 0, kHeaderBytes ); archive->Header = header.Data;
            const uint32_t magic = header.Read32(), version = header.Read32();
            archive->PagedCoverage = magic == kPagedMagic && version == 3;
            Require( archive->PagedCoverage || ( magic == kMagic && version == 2 ), "Unsupported terrain stream format/version" );
            const bool paged = archive->PagedCoverage; const size_t entryBytes = paged ? kPagedEntryBytes : kEntryBytes;
            archive->Config = SettingsBytes( header ); const auto& s = archive->Config;
            const uint32_t base = header.Read32(); Require( base <= LastLevel( s.PatchQuads ), "Invalid terrain coverage stride" ); archive->CoverageLod = static_cast<uint8_t>( base );
            archive->AssetGUID = header.Read64(); archive->Revision = header.Read64(); const uint64_t count = header.Read64(), coverageSize = header.Read64(), coverageHash = header.Read64(), directoryHash = header.Read64(), headerHash = header.Read64();
            Require( Hash( std::span( header.Data ).first( kHeaderBytes - 8 ) ) == headerHash, "Terrain stream header checksum failed" );
            Require( archive->AssetGUID && count == uint64_t( s.TilesX ) * s.TilesZ && count <= kTileLimit && ( paged || PatchCount( s ) <= kPatchLimit ), "Invalid or excessive terrain stream directory" );
            const uint32_t q = s.PatchQuads >> base, levels = LastLevel( q ) + 1, patches = s.TileQuads / s.PatchQuads;
            const uint64_t pageBytes = uint64_t( patches ) * patches * ( 8 + levels * 4 + uint64_t( q + 1 ) * ( q + 1 ) * 16 );
            Require( paged ? ( coverageSize == 0 && coverageHash == 0 && pageBytes <= kCoverageLimit ) : ( coverageSize <= kCoverageLimit && coverageSize == count * pageBytes ), "Invalid terrain coverage size" );
            Require( kHeaderBytes + count * entryBytes + coverageSize <= archive->FileSize, "Truncated terrain coverage" );
            auto directory = Read( file, kHeaderBytes, count * entryBytes ); Require( Hash( directory.Data ) == directoryHash, "Terrain stream directory checksum failed" );
            uint64_t offset = kHeaderBytes + count * entryBytes + coverageSize;
            for( size_t i = 0; i < count; ++i )
            {
                StreamBlock block; block.Coordinate.X = std::bit_cast<int32_t>( directory.Read32() ); block.Coordinate.Z = std::bit_cast<int32_t>( directory.Read32() );
                block.Offset = directory.Read64(); block.Size = directory.Read64(); block.Checksum = directory.Read64(); block.MinHeight = directory.ReadFloat(); block.MaxHeight = directory.ReadFloat();
                if( paged )
                {
                    block.CoverageOffset = directory.Read64(); block.CoverageSize = directory.Read64(); block.CoverageChecksum = directory.Read64();
                    Require( block.CoverageOffset == offset && block.CoverageSize == pageBytes && offset <= archive->FileSize && pageBytes <= archive->FileSize - offset, "Invalid terrain coverage page record" );
                    offset += pageBytes;
                }
                Require( block.Coordinate == Coordinate( s, i ) && block.Offset == offset && block.Size == s.SamplesPerTile() * 16 && block.MinHeight <= block.MaxHeight, "Invalid terrain stream tile record" );
                Require( offset <= archive->FileSize && block.Size <= archive->FileSize - offset, "Truncated terrain detail block" ); offset += block.Size; archive->Blocks.push_back( block );
            }
            Require( offset == archive->FileSize, "Terrain stream archive contains trailing data" );
            if( paged )
            {
                auto data = std::make_shared<ResidentTerrain>(); data->AssetGUID = archive->AssetGUID; data->Streamed = data->PagedCoverage = true;
                data->StreamRevision = headerHash; data->Heights = std::make_shared<Heightfield>( s );
                float min = std::numeric_limits<float>::max(), max = -min;
                for( const auto& block : archive->Blocks ) { min = std::min( min, block.MinHeight ); max = std::max( max, block.MaxHeight ); }
                data->Bounds.Min = Vector3( float( s.MinSample().X * double( s.SampleSpacing ) ), min, float( s.MinSample().Z * double( s.SampleSpacing ) ) );
                data->Bounds.Max = Vector3( float( s.MaxSample().X * double( s.SampleSpacing ) ), max, float( s.MaxSample().Z * double( s.SampleSpacing ) ) );
                archive->Coverage = data; out = archive; error.clear(); return true;
            }
            auto coverage = Read( file, kHeaderBytes + count * entryBytes, coverageSize ); Require( Hash( coverage.Data ) == coverageHash, "Terrain coverage checksum failed" );
            auto data = std::make_shared<ResidentTerrain>(); data->AssetGUID = archive->AssetGUID; data->Streamed = true; data->StreamRevision = headerHash; data->Heights = std::make_shared<Heightfield>( s );
            for( const auto& block : archive->Blocks ) for( uint32_t pz = 0; pz < patches; ++pz ) for( uint32_t px = 0; px < patches; ++px )
            {
                auto patch = MakePatch( s, block.Coordinate, px, pz, static_cast<uint8_t>( base ) );
                const float min = coverage.ReadFloat(), max = coverage.ReadFloat(); Require( min <= max && min >= block.MinHeight && max <= block.MaxHeight, "Invalid terrain coverage bounds" );
                std::vector<float> errors; for( uint32_t l = 0; l < levels; ++l ) { errors.push_back( coverage.ReadFloat() ); Require( errors.back() >= 0, "Negative terrain error" ); }
                for( uint32_t z = 0; z <= q; ++z ) for( uint32_t x = 0; x <= q; ++x )
                {
                    PatchVertex v; v.Position = Vector3( x * s.SampleSpacing * ( 1u << base ), coverage.ReadFloat(), z * s.SampleSpacing * ( 1u << base ) ); v.Normal = ReadNormal( coverage );
                    Require( v.Position.y >= min && v.Position.y <= max, "Terrain coverage vertex exceeds its bounds" ); v.U = float( x ) / q; v.V = float( z ) / q; patch->Mesh.Vertices.push_back( v );
                }
                if( !BuildPatchLods( patch->Mesh, q, error ) ) throw std::runtime_error( error );
                for( size_t l = 0; l < errors.size(); ++l ) patch->Mesh.Lods[l].MaxError = errors[l];
                patch->Mesh.Bounds.Min = Vector3( 0, min, 0 ); patch->Mesh.Bounds.Max = Vector3( s.PatchQuads * s.SampleSpacing, max, s.PatchQuads * s.SampleSpacing );
                auto bounds = patch->Mesh.Bounds; bounds.Min += patch->Offset; bounds.Max += patch->Offset; data->Bounds.Encapsulate( bounds ); data->Patches.push_back( patch );
            }
            BuildPatchNeighbors( *data ); archive->Coverage = data; out = archive; error.clear(); return true;
        }
        catch( const std::exception& e ) { error = e.what(); return false; }
    }


    bool ReadStreamCoverage( const StreamArchive& archive, TileCoord coord, std::shared_ptr<const StreamCoverageTile>& out, std::string& error )
    {
        try
        {
            Require( archive.PagedCoverage, "This terrain uses resident coverage" );
            const auto& s = archive.Config; const auto& block = archive.Blocks.at( archive.TileIndex( coord ) );
            std::ifstream file( archive.Path, std::ios::binary | std::ios::ate );
            Require( bool( file ) && static_cast<uint64_t>( file.tellg() ) == archive.FileSize, "Terrain coverage archive was removed or replaced; reload it" );
            Require( Read( file, 0, kHeaderBytes ).Data == archive.Header, "Terrain coverage generation changed; reload it" );
            auto bytes = Read( file, block.CoverageOffset, block.CoverageSize );
            Require( Hash( bytes.Data ) == block.CoverageChecksum, "Terrain coverage page checksum failed" );
            auto page = std::make_shared<StreamCoverageTile>(); page->Coordinate = coord;
            const uint32_t q = s.PatchQuads >> archive.CoverageLod, patches = s.TileQuads / s.PatchQuads, levels = LastLevel( q ) + 1;
            for( uint32_t pz = 0; pz < patches; ++pz ) for( uint32_t px = 0; px < patches; ++px )
            {
                auto patch = MakePatch( s, coord, px, pz, archive.CoverageLod );
                const float min = bytes.ReadFloat(), max = bytes.ReadFloat();
                Require( min <= max && min >= block.MinHeight && max <= block.MaxHeight, "Invalid coverage page bounds" );
                std::vector<float> errors;
                for( uint32_t l = 0; l < levels; ++l ) { errors.push_back( bytes.ReadFloat() ); Require( errors.back() >= 0, "Negative coverage error" ); }
                for( uint32_t z = 0; z <= q; ++z ) for( uint32_t x = 0; x <= q; ++x )
                {
                    PatchVertex v; v.Position = Vector3( x * s.SampleSpacing * ( 1u << archive.CoverageLod ), bytes.ReadFloat(), z * s.SampleSpacing * ( 1u << archive.CoverageLod ) );
                    v.Normal = ReadNormal( bytes ); v.U = float( x ) / q; v.V = float( z ) / q;
                    Require( v.Position.y >= min && v.Position.y <= max, "Coverage page vertex exceeds its bounds" ); patch->Mesh.Vertices.push_back( v );
                }
                if( !BuildPatchLods( patch->Mesh, q, error ) ) throw std::runtime_error( error );
                for( size_t l = 0; l < errors.size(); ++l ) patch->Mesh.Lods[l].MaxError = errors[l];
                patch->Mesh.Bounds.Min = Vector3( 0, min, 0 ); patch->Mesh.Bounds.Max = Vector3( s.PatchQuads * s.SampleSpacing, max, s.PatchQuads * s.SampleSpacing );
                page->Patches.push_back( patch );
            }
            Require( bytes.Position == bytes.Data.size(), "Trailing coverage page data" );
            out = page; error.clear(); return true;
        }
        catch( const std::exception& e ) { error = e.what(); return false; }
    }


    bool ReadStreamTile( const StreamArchive& archive, TileCoord coord, std::shared_ptr<const StreamTile>& out, std::string& error )
    {
        try
        {
            const auto& s = archive.Config; const auto& block = archive.Blocks.at( archive.TileIndex( coord ) );
            std::ifstream file( archive.Path, std::ios::binary | std::ios::ate ); Require( bool( file ) && static_cast<uint64_t>( file.tellg() ) == archive.FileSize, "Terrain stream archive was removed or replaced; reload it" );
            Require( Read( file, 0, kHeaderBytes ).Data == archive.Header, "Terrain stream archive generation changed; reload it" );
            auto bytes = Read( file, block.Offset, block.Size ); Require( Hash( bytes.Data ) == block.Checksum, "Terrain detail checksum failed" );
            auto tile = std::make_shared<StreamTile>(); tile->Coordinate = coord; tile->Heights.reserve( s.SamplesPerTile() ); std::vector<Vector3> normals; normals.reserve( s.SamplesPerTile() );
            float min = std::numeric_limits<float>::max(), max = -min;
            for( size_t i = 0; i < s.SamplesPerTile(); ++i )
            { const float h = bytes.ReadFloat(); tile->Heights.push_back( h ); normals.push_back( ReadNormal( bytes ) ); min = std::min( min, h ); max = std::max( max, h ); }
            Require( min == block.MinHeight && max == block.MaxHeight, "Terrain detail bounds disagree with its heights" );
            const uint32_t q = s.PatchQuads, side = s.TileQuads + 1, patches = s.TileQuads / q;
            for( uint32_t pz = 0; pz < patches; ++pz ) for( uint32_t px = 0; px < patches; ++px )
            {
                auto patch = MakePatch( s, coord, px, pz, 0 );
                for( uint32_t z = 0; z <= q; ++z ) for( uint32_t x = 0; x <= q; ++x )
                {
                    const size_t i = size_t( pz * q + z ) * side + px * q + x;
                    PatchVertex v; v.Position = Vector3( x * s.SampleSpacing, tile->Heights[i], z * s.SampleSpacing ); v.Normal = normals[i]; v.U = float( x ) / q; v.V = float( z ) / q;
                    patch->Mesh.Bounds.Encapsulate( v.Position ); patch->Mesh.Vertices.push_back( v );
                }
                if( !BuildPatchLods( patch->Mesh, q, error ) ) throw std::runtime_error( error );
                if( !archive.PagedCoverage )
                {
                    const auto& coarse = archive.Coverage->Patches[archive.TileIndex( coord ) * patches * patches + pz * patches + px];
                    Require( patch->Mesh.Bounds.Min.y == coarse->Mesh.Bounds.Min.y && patch->Mesh.Bounds.Max.y == coarse->Mesh.Bounds.Max.y, "Terrain detail and coverage bounds disagree" );
                    for( size_t level = 0; level < coarse->Mesh.Lods.size(); ++level ) Require( patch->Mesh.Lods[coarse->BaseLod + level].MaxError == coarse->Mesh.Lods[level].MaxError, "Terrain detail and coverage errors disagree" );
                    const uint32_t stride = 1u << coarse->BaseLod, coarseSide = ( q >> coarse->BaseLod ) + 1;
                    for( uint32_t z = 0; z <= q; z += stride ) for( uint32_t x = 0; x <= q; x += stride )
                    {
                        const auto& fine = patch->Mesh.Vertices[size_t( z ) * ( q + 1 ) + x]; const auto& low = coarse->Mesh.Vertices[size_t( z / stride ) * coarseSide + x / stride];
                        Require( fine.Position.y == low.Position.y && fine.Normal.InternalVector == low.Normal.InternalVector, "Terrain detail and coverage samples disagree" );
                    }
                }
                tile->Patches.push_back( patch );
            }
            auto collision = std::make_shared<CollisionTile>(); collision->Coordinate = coord; collision->Positions.reserve( s.SamplesPerTile() ); collision->Indices.reserve( size_t( s.TileQuads ) * s.TileQuads * 6 );
            for( uint32_t z = 0; z < side; ++z ) for( uint32_t x = 0; x < side; ++x ) collision->Positions.emplace_back( static_cast<float>( ( int64_t( coord.X ) * s.TileQuads + x ) * double( s.SampleSpacing ) ), tile->Heights[size_t( z ) * side + x], static_cast<float>( ( int64_t( coord.Z ) * s.TileQuads + z ) * double( s.SampleSpacing ) ) );
            for( uint32_t z = 0; z < s.TileQuads; ++z ) for( uint32_t x = 0; x < s.TileQuads; ++x ) { const uint32_t a = z * side + x, b = a + side; collision->Indices.insert( collision->Indices.end(), { a, b, a + 1, a + 1, b, b + 1 } ); }
            tile->Collision = collision; out = tile; error.clear(); return true;
        }
        catch( const std::exception& e ) { error = e.what(); return false; }
    }
}
