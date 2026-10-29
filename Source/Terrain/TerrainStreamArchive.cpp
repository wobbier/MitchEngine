#include "PCH.h"
#include "TerrainStreamArchive.h"
#include "TerrainSurface.h"
#include "TerrainStreamCodec.h"
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
        constexpr uint32_t kSurfaceMagic = 0x3453544d; // MTS4.
        constexpr uint32_t kCompressedMagic = 0x3553544d; // MTS5.
        constexpr uint32_t kHierarchyMagic = 0x3653544d; // MTS6.
        constexpr size_t kHierarchyHeaderBytes = 128, kNodeBytes = 64;
        constexpr uint64_t kPaletteLimit = 32768 + 24;
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
            Require( size <= kCoverageLimit + 8, "Terrain stream read exceeds the bounded block limit" );
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

        std::shared_ptr<ResidentPatch> MakePatch( const Settings& s, TileCoord tile, uint32_t px, uint32_t pz, uint8_t base, uint8_t span = 0 )
        {
            auto patch = std::make_shared<ResidentPatch>(); patch->BaseLod = base + span; patch->SpanLevel = span;
            patch->Mesh.SampleOrigin = { int64_t( tile.X ) * s.TileQuads + ( int64_t( px ) * s.PatchQuads << span ), int64_t( tile.Z ) * s.TileQuads + ( int64_t( pz ) * s.PatchQuads << span ) };
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


    bool IsStreamArchiveFile( const std::string& InFullPath )
    {
        std::ifstream file( InFullPath, std::ios::binary );
        std::array<uint8_t, 4> prefix {};
        if( !file.read( reinterpret_cast<char*>( prefix.data() ), prefix.size() ) )
        {
            return false;
        }
        return IsStreamArchive( prefix );
    }


    bool IsStreamArchive( std::span<const uint8_t> prefix )
    {
        return prefix.size() >= 4 && prefix[0] == 'M' && prefix[1] == 'T' && prefix[2] == 'S' && ( prefix[3] == '2' || prefix[3] == '3' || prefix[3] == '4' || prefix[3] == '5' || prefix[3] == '6' );
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


    static bool CookArchive( const std::filesystem::path& path, const Heightfield& field, uint64_t guid, uint8_t base, bool paged, bool compressed, bool hierarchical, std::string& error )
    {
        std::filesystem::path temporary;
        try
        {
            Require( !compressed || paged, "Compressed terrain requires paged coverage" );
            const auto& surface = field.GetSurface();
            Require( !surface || paged, "Layered terrain requires the paged streaming cooker" );
            if( surface && !surface->Validate( field.GetSettings(), error ) ) throw std::runtime_error( error );
            Bytes palette;
            if( surface )
            {
                Surface metadata; metadata.Layers = surface->Layers; metadata.BaseWeights = surface->BaseWeights;
                if( !SerializeSurface( metadata, field.GetSettings(), palette.Data, error ) ) throw std::runtime_error( error );
            }
            const auto& s = field.GetSettings(); const uint64_t count = uint64_t( s.TilesX ) * s.TilesZ;
            Require( path.extension() == ".terrainc" && guid && count <= kTileLimit && field.GetTiles().size() == count, "Stream cooking requires a .terrainc destination, GUID and every source tile" );
            Require( s.TileQuads <= 256 && base <= LastLevel( s.PatchQuads ) && ( paged || PatchCount( s ) <= kPatchLimit ), "Stream cooking requires tiles <= 256 quads and bounded patch coverage" );
            for( int64_t sample : { s.MinSample().X, s.MinSample().Z, s.MaxSample().X, s.MaxSample().Z } )
            {
                const float position = static_cast<float>( sample * double( s.SampleSpacing ) );
                Require( std::isfinite( position ) && position + s.SampleSpacing != position, "Terrain stream coordinates lose canonical Float32 precision" );
            }
            const uint32_t q = s.PatchQuads >> base, levels = LastLevel( q ) + 1, patches = s.TileQuads / s.PatchQuads;
            const uint64_t weightBytes = surface ? uint64_t( s.PatchQuads + 1 ) * ( s.PatchQuads + 1 ) * 4 : 0;
            std::vector<CoverageNode> nodes; std::vector<uint32_t> roots;
            const size_t headerBytes = hierarchical ? kHierarchyHeaderBytes : kHeaderBytes;
            if( hierarchical )
            {
                Require( base < LastLevel( s.PatchQuads ), "Hierarchical coverage needs at least two grid quads per patch" );
                if( !BuildCoverageHierarchy( s, nodes, roots, error ) ) throw std::runtime_error( error );
            }
            const uint64_t coverageSize = PatchCount( s ) * ( 8 + levels * 4 + uint64_t( q + 1 ) * ( q + 1 ) * 16 + weightBytes );
            Require( ( paged ? coverageSize / count : coverageSize ) <= kCoverageLimit, "Terrain coarse coverage exceeds 64 MiB; increase its stride or split the dataset" );
            Require( !hierarchical || roots.size() * ( coverageSize / count ) <= kCoverageLimit, "Terrain root coverage exceeds 64 MiB; use a smaller/coarser dataset" );
            if( !path.parent_path().empty() ) std::filesystem::create_directories( path.parent_path() );
            temporary = path; temporary += "." + std::to_string( EngineGUID::Generate() ) + ".tmp";
            std::ofstream file( temporary, std::ios::binary | std::ios::trunc ); Require( bool( file ), "Could not stage terrain cook" );
            Bytes coverage, directory; coverage.Data.reserve( static_cast<size_t>( paged ? coverageSize / count : coverageSize ) );
            uint64_t offset = headerBytes + nodes.size() * kNodeBytes + count * ( paged ? kPagedEntryBytes : kEntryBytes ) + ( paged ? palette.Data.size() : coverageSize ), revision = surface ? Hash( palette.Data ) : kHashSeed;
            size_t leafIndex = 0;
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
                    if( surface ) for( const auto& weights : BuildSurfacePatch( *surface, mesh.SampleOrigin, s.PatchQuads ) )
                        coverage.Data.insert( coverage.Data.end(), weights.begin(), weights.end() );
                }
                const uint64_t coverageOffset = paged ? offset : 0;
                const uint64_t coverageHash = paged ? Hash( coverage.Data ) : 0, payloadHash = Hash( payload.Data );
                if( paged && surface ) revision = Hash( coverage.Data, revision );
                revision = Hash( payload.Data, revision );
                if( compressed && ( !EncodeStreamBlock( coverage.Data, coverage.Data, error ) || !EncodeStreamBlock( payload.Data, payload.Data, error ) ) ) throw std::runtime_error( error );
                if( paged ) { Write( file, offset, coverage ); offset += coverage.Data.size(); }
                directory.U32( std::bit_cast<uint32_t>( tile.X ) ); directory.U32( std::bit_cast<uint32_t>( tile.Z ) );
                directory.U64( offset ); directory.U64( payload.Data.size() ); directory.U64( payloadHash ); directory.F32( min ); directory.F32( max );
                if( hierarchical )
                {
                    auto& node = nodes[leafIndex++]; node.Offset = coverageOffset; node.Size = coverage.Data.size(); node.Checksum = coverageHash;
                    node.DecodedSize = coverageSize / count; node.MinHeight = min; node.MaxHeight = max;
                }
                if( paged ) { directory.U64( coverageOffset ); directory.U64( coverage.Data.size() ); directory.U64( coverageHash ); coverage.Data.clear(); }
                Write( file, offset, payload ); offset += payload.Data.size();
            }
            Bytes nodeDirectory;
            if( hierarchical )
            {
                for( size_t i = count; i < nodes.size(); ++i )
                {
                    auto& node = nodes[i]; std::shared_ptr<const StreamCoverageTile> page;
                    if( !BuildHierarchyPage( field, node, base, page, error ) ) throw std::runtime_error( error );
                    Bytes bytes; node.MinHeight = std::numeric_limits<float>::max(); node.MaxHeight = -node.MinHeight;
                    for( const auto& patch : page->Patches )
                    {
                        bytes.F32( patch->Mesh.Bounds.Min.y ); bytes.F32( patch->Mesh.Bounds.Max.y );
                        node.MinHeight = std::min( node.MinHeight, patch->Mesh.Bounds.Min.y ); node.MaxHeight = std::max( node.MaxHeight, patch->Mesh.Bounds.Max.y );
                        for( const auto& lod : patch->Mesh.Lods ) bytes.F32( lod.MaxError );
                        for( const auto& v : patch->Mesh.Vertices ) { bytes.F32( v.Position.y ); bytes.F32( v.Normal.x ); bytes.F32( v.Normal.y ); bytes.F32( v.Normal.z ); }
                        for( const auto& weights : patch->Weights ) bytes.Data.insert( bytes.Data.end(), weights.begin(), weights.end() );
                    }
                    Require( bytes.Data.size() == coverageSize / count, "Internal hierarchy page size mismatch" );
                    node.DecodedSize = bytes.Data.size(); node.Checksum = Hash( bytes.Data );
                    if( !EncodeStreamBlock( bytes.Data, bytes.Data, error ) ) throw std::runtime_error( error );
                    node.Offset = offset; node.Size = bytes.Data.size(); Write( file, offset, bytes ); offset += node.Size;
                }
                for( const auto& node : nodes )
                {
                    nodeDirectory.U32( std::bit_cast<uint32_t>( node.Coordinate.X ) ); nodeDirectory.U32( std::bit_cast<uint32_t>( node.Coordinate.Z ) );
                    nodeDirectory.U32( node.Level ); nodeDirectory.U32( std::bit_cast<uint32_t>( node.Parent ) );
                    for( int32_t child : node.Children ) nodeDirectory.U32( std::bit_cast<uint32_t>( child ) );
                    nodeDirectory.U64( node.Offset ); nodeDirectory.U64( node.Size ); nodeDirectory.U64( node.Checksum ); nodeDirectory.F32( node.MinHeight ); nodeDirectory.F32( node.MaxHeight );
                }
            }
            Require( paged || coverage.Data.size() == coverageSize, "Internal terrain coverage size mismatch" );
            Bytes header; header.U32( hierarchical ? kHierarchyMagic : ( compressed ? kCompressedMagic : ( surface ? kSurfaceMagic : ( paged ? kPagedMagic : kMagic ) ) ) ); header.U32( hierarchical ? 6 : ( compressed ? 5 : ( surface ? 4 : ( paged ? 3 : 2 ) ) ) ); SettingsBytes( header, s ); header.U32( base ); header.U64( guid ); header.U64( revision );
            header.U64( count ); header.U64( paged ? palette.Data.size() : coverageSize ); header.U64( surface ? Hash( palette.Data ) : ( paged ? 0 : Hash( coverage.Data ) ) ); header.U64( Hash( directory.Data ) );
            if( hierarchical ) { header.U64( nodes.size() ); header.U64( Hash( nodeDirectory.Data ) ); header.U64( roots.size() ); }
            header.U64( Hash( header.Data ) );
            Require( header.Data.size() == headerBytes, "Internal terrain stream header mismatch" );
            Write( file, 0, header ); Write( file, headerBytes, directory );
            if( hierarchical ) Write( file, headerBytes + directory.Data.size(), nodeDirectory );
            const uint64_t metadataEnd = headerBytes + directory.Data.size() + nodeDirectory.Data.size();
            if( paged ) Write( file, metadataEnd, palette ); else Write( file, metadataEnd, coverage );
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
        return CookArchive( path, field, guid, base, false, false, false, error );
    }


    bool CookPagedStreamArchive( const std::filesystem::path& path, const Heightfield& field, uint64_t guid, uint8_t base, std::string& error )
    {
        return CookArchive( path, field, guid, base, true, false, false, error );
    }


    bool CookCompressedStreamArchive( const std::filesystem::path& path, const Heightfield& field, uint64_t guid, uint8_t base, std::string& error )
    {
        return CookArchive( path, field, guid, base, true, true, false, error );
    }


    bool CookHierarchicalStreamArchive( const std::filesystem::path& path, const Heightfield& field, uint64_t guid, uint8_t base, std::string& error )
    {
        return CookArchive( path, field, guid, base, true, true, true, error );
    }


    bool OpenStreamArchive( const std::filesystem::path& path, std::shared_ptr<const StreamArchive>& out, std::string& error )
    {
        try
        {
            std::ifstream file( path, std::ios::binary | std::ios::ate ); Require( bool( file ), "Could not open terrain stream archive" );
            const auto size = file.tellg(); Require( size >= static_cast<std::streamoff>( kHeaderBytes ), "Truncated terrain stream header" );
            auto archive = std::make_shared<StreamArchive>(); archive->Path = path; archive->FileSize = static_cast<uint64_t>( size );
            auto header = Read( file, 0, kHeaderBytes );
            if( header.Data[3] == '6' ) header = Read( file, 0, kHierarchyHeaderBytes );
            archive->Header = header.Data; const size_t headerBytes = header.Data.size();
            const uint32_t magic = header.Read32(), version = header.Read32();
            bool layered = magic == kSurfaceMagic && version == 4;
            archive->Hierarchical = magic == kHierarchyMagic && version == 6;
            archive->Compressed = archive->Hierarchical || ( magic == kCompressedMagic && version == 5 );
            archive->PagedCoverage = archive->Compressed || layered || ( magic == kPagedMagic && version == 3 );
            Require( archive->PagedCoverage || ( magic == kMagic && version == 2 ), "Unsupported terrain stream format/version" );
            const bool paged = archive->PagedCoverage; const size_t entryBytes = paged ? kPagedEntryBytes : kEntryBytes;
            archive->Config = SettingsBytes( header ); const auto& s = archive->Config;
            const uint32_t base = header.Read32(); Require( base <= LastLevel( s.PatchQuads ), "Invalid terrain coverage stride" ); archive->CoverageLod = static_cast<uint8_t>( base );
            archive->AssetGUID = header.Read64(); archive->Revision = header.Read64(); const uint64_t count = header.Read64(), coverageSize = header.Read64(), coverageHash = header.Read64(), directoryHash = header.Read64();
            uint64_t nodeCount = 0, nodeHash = 0, rootCount = 0;
            if( archive->Hierarchical ) { nodeCount = header.Read64(); nodeHash = header.Read64(); rootCount = header.Read64(); }
            const uint64_t headerHash = header.Read64();
            Require( Hash( std::span( header.Data ).first( headerBytes - 8 ) ) == headerHash, "Terrain stream header checksum failed" );
            if( archive->Hierarchical )
            {
                Require( base < LastLevel( s.PatchQuads ) && nodeCount <= 2 * kTileLimit, "Invalid hierarchy directory count or grid" );
                if( !BuildCoverageHierarchy( s, archive->Nodes, archive->Roots, error ) ) throw std::runtime_error( error );
                Require( nodeCount == archive->Nodes.size() && rootCount == archive->Roots.size(), "Invalid hierarchy topology counts" );
            }
            layered |= archive->Compressed && coverageSize != 0;
            Require( archive->AssetGUID && count == uint64_t( s.TilesX ) * s.TilesZ && count <= kTileLimit && ( paged || PatchCount( s ) <= kPatchLimit ), "Invalid or excessive terrain stream directory" );
            const uint32_t q = s.PatchQuads >> base, levels = LastLevel( q ) + 1, patches = s.TileQuads / s.PatchQuads;
            const uint64_t weightBytes = layered ? uint64_t( s.PatchQuads + 1 ) * ( s.PatchQuads + 1 ) * 4 : 0;
            const uint64_t pageBytes = uint64_t( patches ) * patches * ( 8 + levels * 4 + uint64_t( q + 1 ) * ( q + 1 ) * 16 + weightBytes );
            Require( paged ? ( pageBytes <= kCoverageLimit && ( layered ? ( coverageSize >= 24 && coverageSize <= kPaletteLimit ) : ( coverageSize == 0 && coverageHash == 0 ) ) ) :
                ( coverageSize <= kCoverageLimit && coverageSize == count * pageBytes ), "Invalid terrain coverage size" );
            Require( headerBytes + count * entryBytes + nodeCount * kNodeBytes + coverageSize <= archive->FileSize, "Truncated terrain coverage" );
            auto directory = Read( file, headerBytes, count * entryBytes ); Require( Hash( directory.Data ) == directoryHash, "Terrain stream directory checksum failed" );
            const uint64_t metadataEnd = headerBytes + count * entryBytes + nodeCount * kNodeBytes;
            uint64_t offset = metadataEnd + coverageSize;
            for( size_t i = 0; i < count; ++i )
            {
                StreamBlock block; block.Coordinate.X = std::bit_cast<int32_t>( directory.Read32() ); block.Coordinate.Z = std::bit_cast<int32_t>( directory.Read32() );
                block.Offset = directory.Read64(); block.Size = directory.Read64(); block.Checksum = directory.Read64(); block.MinHeight = directory.ReadFloat(); block.MaxHeight = directory.ReadFloat();
                if( paged )
                {
                    block.CoverageOffset = directory.Read64(); block.CoverageSize = directory.Read64(); block.CoverageChecksum = directory.Read64();
                    block.CoverageDecodedSize = pageBytes;
                    Require( block.CoverageOffset == offset && ( archive->Compressed ? ( block.CoverageSize >= 8 && block.CoverageSize <= pageBytes + 8 ) : block.CoverageSize == pageBytes ) &&
                        offset <= archive->FileSize && block.CoverageSize <= archive->FileSize - offset, "Invalid terrain coverage page record" );
                    offset += block.CoverageSize;
                }
                block.DecodedSize = s.SamplesPerTile() * 16;
                Require( block.Coordinate == Coordinate( s, i ) && block.Offset == offset && ( archive->Compressed ? ( block.Size >= 8 && block.Size <= block.DecodedSize + 8 ) : block.Size == block.DecodedSize ) && block.MinHeight <= block.MaxHeight, "Invalid terrain stream tile record" );
                Require( offset <= archive->FileSize && block.Size <= archive->FileSize - offset, "Truncated terrain detail block" ); offset += block.Size; archive->Blocks.push_back( block );
            }
            if( archive->Hierarchical )
            {
                Require( rootCount * pageBytes <= kCoverageLimit, "Terrain resident roots exceed the bounded limit" );
                auto records = Read( file, headerBytes + count * entryBytes, nodeCount * kNodeBytes );
                Require( Hash( records.Data ) == nodeHash, "Terrain hierarchy directory checksum failed" );
                for( size_t i = 0; i < archive->Nodes.size(); ++i )
                {
                    auto& n = archive->Nodes[i];
                    const int32_t x = std::bit_cast<int32_t>( records.Read32() ), z = std::bit_cast<int32_t>( records.Read32() ); const uint32_t level = records.Read32();
                    Require( x == n.Coordinate.X && z == n.Coordinate.Z && level == n.Level && std::bit_cast<int32_t>( records.Read32() ) == n.Parent, "Invalid terrain hierarchy address or parent" );
                    for( auto child : n.Children ) Require( std::bit_cast<int32_t>( records.Read32() ) == child, "Invalid terrain hierarchy child" );
                    n.Offset = records.Read64(); n.Size = records.Read64(); n.Checksum = records.Read64(); n.DecodedSize = pageBytes; n.MinHeight = records.ReadFloat(); n.MaxHeight = records.ReadFloat();
                    Require( n.MinHeight <= n.MaxHeight && n.Size >= 8 && n.Size <= pageBytes + 8, "Invalid hierarchy bounds or stored size" );
                    if( i < count )
                    {
                        const auto& leaf = archive->Blocks[i];
                        Require( n.Offset == leaf.CoverageOffset && n.Size == leaf.CoverageSize && n.Checksum == leaf.CoverageChecksum && n.MinHeight == leaf.MinHeight && n.MaxHeight == leaf.MaxHeight, "Hierarchy leaf disagrees with canonical directory" );
                    }
                    else
                    {
                        Require( n.Offset == offset && offset <= archive->FileSize && n.Size <= archive->FileSize - offset, "Invalid hierarchy page range" ); offset += n.Size;
                        float minimum = std::numeric_limits<float>::infinity(), maximum = -minimum;
                        for( auto child : n.Children ) { minimum = std::min( minimum, archive->Nodes[child].MinHeight ); maximum = std::max( maximum, archive->Nodes[child].MaxHeight ); }
                        Require( n.MinHeight == minimum && n.MaxHeight == maximum, "Hierarchy bounds disagree with canonical children" );
                    }
                }
            }
            Require( offset == archive->FileSize, "Terrain stream archive contains trailing data" );
            if( paged )
            {
                auto data = std::make_shared<ResidentTerrain>(); data->AssetGUID = archive->AssetGUID; data->Streamed = data->PagedCoverage = true; data->Hierarchical = archive->Hierarchical;
                data->StreamRevision = headerHash; auto heights = std::make_shared<Heightfield>( s );
                if( layered )
                {
                    auto palette = Read( file, metadataEnd, coverageSize );
                    Require( Hash( palette.Data ) == coverageHash, "Terrain surface palette checksum failed" );
                    auto surface = std::make_shared<Surface>();
                    if( !DeserializeSurface( palette.Data, s, *surface, error ) ) throw std::runtime_error( error );
                    Require( surface->Samples.empty(), "Stream metadata must not contain painted samples" );
                    if( !heights->SetSurface( surface, error ) ) throw std::runtime_error( error );
                }
                data->Heights = heights;
                float min = std::numeric_limits<float>::max(), max = -min;
                for( const auto& block : archive->Blocks ) { min = std::min( min, block.MinHeight ); max = std::max( max, block.MaxHeight ); }
                data->Bounds.Min = Vector3( float( s.MinSample().X * double( s.SampleSpacing ) ), min, float( s.MinSample().Z * double( s.SampleSpacing ) ) );
                data->Bounds.Max = Vector3( float( s.MaxSample().X * double( s.SampleSpacing ) ), max, float( s.MaxSample().Z * double( s.SampleSpacing ) ) );
                archive->Coverage = data;
                if( archive->Hierarchical )
                {
                    for( auto root : archive->Roots )
                    {
                        std::shared_ptr<const StreamCoverageTile> page;
                        if( !ReadHierarchyCoverage( *archive, root, page, error ) ) throw std::runtime_error( error );
                        data->CoverageNodes.push_back( root ); data->CoverageTiles.push_back( archive->Nodes[root].Coordinate );
                        data->Patches.insert( data->Patches.end(), page->Patches.begin(), page->Patches.end() );
                    }
                    data->CoveragePatches = data->Patches; BuildHierarchyNeighbors( *data );
                }
                out = archive; error.clear(); return true;
            }
            auto coverage = Read( file, metadataEnd, coverageSize ); Require( Hash( coverage.Data ) == coverageHash, "Terrain coverage checksum failed" );
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


    static bool ReadCoveragePage( const StreamArchive& archive, TileCoord coord, int32_t nodeID, std::shared_ptr<const StreamCoverageTile>& out, std::string& error )
    {
        try
        {
            Require( archive.PagedCoverage, "This terrain uses resident coverage" );
            const auto& s = archive.Config; const auto& block = archive.Blocks.at( archive.TileIndex( coord ) );
            const auto* node = nodeID >= 0 ? &archive.Nodes.at( size_t( nodeID ) ) : nullptr;
            const uint64_t pageOffset = node ? node->Offset : block.CoverageOffset, pageSize = node ? node->Size : block.CoverageSize;
            const uint64_t decodedSize = node ? node->DecodedSize : block.CoverageDecodedSize, checksum = node ? node->Checksum : block.CoverageChecksum;
            const float nodeMin = node ? node->MinHeight : block.MinHeight, nodeMax = node ? node->MaxHeight : block.MaxHeight;
            const uint8_t span = node ? node->Level : 0;
            std::ifstream file( archive.Path, std::ios::binary | std::ios::ate );
            Require( bool( file ) && static_cast<uint64_t>( file.tellg() ) == archive.FileSize, "Terrain coverage archive was removed or replaced; reload it" );
            Require( Read( file, 0, archive.Header.size() ).Data == archive.Header, "Terrain coverage generation changed; reload it" );
            auto bytes = Read( file, pageOffset, pageSize );
            if( archive.Compressed && !DecodeStreamBlock( bytes.Data, decodedSize, bytes.Data, error ) ) throw std::runtime_error( error );
            Require( Hash( bytes.Data ) == checksum, "Terrain coverage page checksum failed" );
            auto page = std::make_shared<StreamCoverageTile>(); page->Coordinate = coord;
            const uint32_t q = s.PatchQuads >> archive.CoverageLod, patches = s.TileQuads / s.PatchQuads, levels = LastLevel( q ) + 1;
            for( uint32_t pz = 0; pz < patches; ++pz ) for( uint32_t px = 0; px < patches; ++px )
            {
                auto patch = MakePatch( s, coord, px, pz, archive.CoverageLod, span );
                const float min = bytes.ReadFloat(), max = bytes.ReadFloat();
                Require( min <= max && min >= nodeMin && max <= nodeMax, "Invalid coverage page bounds" );
                std::vector<float> errors;
                for( uint32_t l = 0; l < levels; ++l ) { errors.push_back( bytes.ReadFloat() ); Require( errors.back() >= 0, "Negative coverage error" ); }
                for( uint32_t z = 0; z <= q; ++z ) for( uint32_t x = 0; x <= q; ++x )
                {
                    PatchVertex v; v.Position = Vector3( x * s.SampleSpacing * ( 1u << ( archive.CoverageLod + span ) ), bytes.ReadFloat(), z * s.SampleSpacing * ( 1u << ( archive.CoverageLod + span ) ) );
                    v.Normal = ReadNormal( bytes ); v.U = float( x ) / q; v.V = float( z ) / q;
                    Require( v.Position.y >= min && v.Position.y <= max, "Coverage page vertex exceeds its bounds" ); patch->Mesh.Vertices.push_back( v );
                }
                if( !BuildPatchLods( patch->Mesh, q, error ) ) throw std::runtime_error( error );
                for( size_t l = 0; l < errors.size(); ++l ) patch->Mesh.Lods[l].MaxError = errors[l];
                patch->Mesh.Bounds.Min = Vector3( 0, min, 0 ); patch->Mesh.Bounds.Max = Vector3( ( uint64_t( s.PatchQuads ) << span ) * s.SampleSpacing, max, ( uint64_t( s.PatchQuads ) << span ) * s.SampleSpacing );
                if( archive.Coverage->Heights->GetSurface() )
                {
                    const size_t samples = size_t( s.PatchQuads + 1 ) * ( s.PatchQuads + 1 );
                    patch->Weights.reserve( samples );
                    for( size_t i = 0; i < samples; ++i )
                    {
                        Require( bytes.Position <= bytes.Data.size() && bytes.Data.size() - bytes.Position >= 4, "Truncated terrain surface weights" );
                        std::array<uint8_t, 4> weights; std::copy_n( bytes.Data.begin() + bytes.Position, 4, weights.begin() ); bytes.Position += 4;
                        Require( unsigned( weights[0] ) + weights[1] + weights[2] + weights[3] == 255, "Terrain page weights are not normalized" );
                        patch->Weights.push_back( weights );
                    }
                }
                page->Patches.push_back( patch );
            }
            Require( bytes.Position == bytes.Data.size(), "Trailing coverage page data" );
            out = page; error.clear(); return true;
        }
        catch( const std::exception& e ) { error = e.what(); return false; }
    }


    bool ReadStreamCoverage( const StreamArchive& archive, TileCoord coord, std::shared_ptr<const StreamCoverageTile>& out, std::string& error )
    {
        if( !archive.Config.Contains( coord ) ) { error = "Coverage tile is outside the archive"; return false; }
        return ReadCoveragePage( archive, coord, archive.Hierarchical ? int32_t( archive.TileIndex( coord ) ) : -1, out, error );
    }


    bool ReadHierarchyCoverage( const StreamArchive& archive, uint32_t node, std::shared_ptr<const StreamCoverageTile>& out, std::string& error )
    {
        if( !archive.Hierarchical || node >= archive.Nodes.size() ) { error = "Invalid hierarchy coverage node"; return false; }
        return ReadCoveragePage( archive, archive.Nodes[node].Coordinate, int32_t( node ), out, error );
    }


    static std::vector<float> CanonicalHeights( const StreamArchive& archive, TileCoord coord, std::vector<Vector3>* normals )
    {
        const auto& s = archive.Config; const auto& block = archive.Blocks.at( archive.TileIndex( coord ) );
        std::ifstream file( archive.Path, std::ios::binary | std::ios::ate ); Require( bool( file ) && static_cast<uint64_t>( file.tellg() ) == archive.FileSize, "Terrain stream archive was removed or replaced; reload it" );
        Require( Read( file, 0, archive.Header.size() ).Data == archive.Header, "Terrain stream archive generation changed; reload it" );
        auto bytes = Read( file, block.Offset, block.Size );
        std::string error;
        if( archive.Compressed && !DecodeStreamBlock( bytes.Data, block.DecodedSize, bytes.Data, error ) ) throw std::runtime_error( error );
        Require( Hash( bytes.Data ) == block.Checksum, "Terrain detail checksum failed" );
        std::vector<float> heights; heights.reserve( s.SamplesPerTile() ); if( normals ) normals->reserve( s.SamplesPerTile() );
        float min = std::numeric_limits<float>::max(), max = -min;
        for( size_t i = 0; i < s.SamplesPerTile(); ++i )
        {
            const float h = bytes.ReadFloat(); const auto normal = ReadNormal( bytes ); heights.push_back( h ); if( normals ) normals->push_back( normal );
            min = std::min( min, h ); max = std::max( max, h );
        }
        Require( min == block.MinHeight && max == block.MaxHeight, "Terrain detail bounds disagree with its heights" ); return heights;
    }


    bool ReadStreamHeights( const StreamArchive& archive, TileCoord coord, std::vector<float>& out, std::string& error )
    {
        try { auto heights = CanonicalHeights( archive, coord, nullptr ); out = std::move( heights ); error.clear(); return true; }
        catch( const std::exception& e ) { error = e.what(); return false; }
    }


    bool ReadStreamTile( const StreamArchive& archive, TileCoord coord, std::shared_ptr<const StreamTile>& out, std::string& error )
    {
        try
        {
            const auto& s = archive.Config;
            auto tile = std::make_shared<StreamTile>(); tile->Coordinate = coord; std::vector<Vector3> normals;
            tile->Heights = CanonicalHeights( archive, coord, &normals );
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
