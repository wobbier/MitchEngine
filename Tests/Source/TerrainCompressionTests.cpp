#include <doctest/doctest.h>
#include "Terrain/TerrainStreamCodec.h"
#include "Terrain/TerrainStreamCache.h"
#include "Terrain/TerrainSurface.h"
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <iterator>

namespace
{
    struct CompressedFixture
    {
        TerrainData::Heightfield Field;
        std::filesystem::path Directory, File;
        std::shared_ptr<const TerrainData::StreamArchive> Archive;
        CompressedFixture( bool painted = false ) : Field( Settings() )
        {
            static unsigned sequence = 0;
            Directory = std::filesystem::path( ".tmp/Tests/TerrainCompression" ) / std::to_string( ++sequence );
            std::filesystem::remove_all( Directory ); std::filesystem::create_directories( Directory ); File = Directory / "Compressed.terrainc";
            std::string error;
            for( int x = -1; x <= 0; ++x ) for( int z = -1; z <= 0; ++z ) REQUIRE( Field.CreateFlatTile( { x, z }, 0, error ) );
            for( int z = -16; z <= 16; ++z ) for( int x = -16; x <= 16; ++x ) REQUIRE( Field.SetSample( { x, z }, float( std::sin( x * 0.13 ) * std::cos( z * 0.11 ) ), error ) );
            if( painted )
            {
                auto surface = std::make_shared<TerrainData::Surface>(); surface->BaseWeights = { 80, 90, 60, 25 };
                surface->Layers[2].Name = "Stone"; surface->Layers[2].BaseColorMap = "Assets/Textures/Stone.png";
                for( int z = -16; z <= 16; ++z ) for( int x = -16; x <= 16; ++x ) if( ( x + z ) % 3 == 0 ) surface->Set( { x, z }, { 0, 0, 255, 0 } );
                REQUIRE( Field.SetSurface( surface, error ) );
            }
            REQUIRE( TerrainData::CookCompressedStreamArchive( File, Field, 12345, 2, error ) );
            REQUIRE( TerrainData::OpenStreamArchive( File, Archive, error ) );
        }
        static TerrainData::Settings Settings()
        {
            TerrainData::Settings s; s.TileQuads = 16; s.PatchQuads = 8; s.MinTile = { -1, -1 }; s.TilesX = s.TilesZ = 2; return s;
        }
    };


    void SamePatches( const std::vector<std::shared_ptr<const TerrainData::ResidentPatch>>& a, const std::vector<std::shared_ptr<const TerrainData::ResidentPatch>>& b )
    {
        REQUIRE( a.size() == b.size() );
        for( size_t p = 0; p < a.size(); ++p )
        {
            CHECK( a[p]->BaseLod == b[p]->BaseLod ); CHECK( a[p]->Weights == b[p]->Weights );
            CHECK( a[p]->Mesh.SampleOrigin == b[p]->Mesh.SampleOrigin ); REQUIRE( a[p]->Mesh.Vertices.size() == b[p]->Mesh.Vertices.size() );
            for( size_t v = 0; v < a[p]->Mesh.Vertices.size(); ++v )
            {
                CHECK( a[p]->Mesh.Vertices[v].Position.InternalVector == b[p]->Mesh.Vertices[v].Position.InternalVector );
                CHECK( a[p]->Mesh.Vertices[v].Normal.InternalVector == b[p]->Mesh.Vertices[v].Normal.InternalVector );
            }
            REQUIRE( a[p]->Mesh.Lods.size() == b[p]->Mesh.Lods.size() );
            for( size_t l = 0; l < a[p]->Mesh.Lods.size(); ++l ) { CHECK( a[p]->Mesh.Lods[l].MaxError == b[p]->Mesh.Lods[l].MaxError ); CHECK( a[p]->Mesh.Lods[l].Indices == b[p]->Mesh.Lods[l].Indices ); }
        }
    }


    void Byte( const std::filesystem::path& path, uint64_t offset, uint8_t value )
    {
        std::fstream file( path, std::ios::binary | std::ios::in | std::ios::out ); file.seekp( std::streamoff( offset ) ); file.put( char( value ) ); REQUIRE( file.good() );
    }


    void Put64( std::vector<uint8_t>& bytes, size_t offset, uint64_t value )
    {
        for( size_t i = 0; i < 8; ++i ) bytes[offset + i] = uint8_t( value >> ( i * 8 ) );
    }


    uint64_t Checksum( std::span<const uint8_t> bytes )
    {
        uint64_t hash = 14695981039346656037ull; for( const auto value : bytes ) hash = ( hash ^ value ) * 1099511628211ull; return hash;
    }


    void DrainCompressed( TerrainData::TerrainStreamCache& cache, std::span<const TerrainData::TileDemand> demand, const TerrainData::StreamLimits& limits, uint64_t& frame )
    {
        for( int i = 0; i < 30; ++i )
        {
            cache.Tick( ++frame, demand, limits ); cache.WaitForRequest();
            if( !cache.GetStats().Loading && !cache.GetStats().Pending ) break;
        }
    }
}

TEST_CASE( "Terrain compression: byte planes preserve arbitrary bits and bound raw fallback" )
{
    std::string error; std::vector<uint8_t> encoded, decoded;
    for( size_t size : { 0u, 4u, 128u, 260u, 1024u, 65536u } )
    {
        std::vector<uint8_t> bytes( size ); uint32_t random = 0x1a2b3c4d;
        for( auto& value : bytes ) { random ^= random << 13; random ^= random >> 17; random ^= random << 5; value = uint8_t( random ); }
        REQUIRE( TerrainData::EncodeStreamBlock( bytes, encoded, error ) ); CHECK( encoded.size() <= bytes.size() + 8 );
        REQUIRE( TerrainData::DecodeStreamBlock( encoded, size, decoded, error ) ); CHECK( decoded == bytes );
        if( size >= 128 ) CHECK( encoded[0] == 0 );
    }
    // Repeating float bit patterns include signed zero, infinity and NaN; the codec never interprets them.
    std::vector<uint8_t> bytes;
    for( int n = 0; n < 1024; ++n ) for( uint32_t word : { 0x80000000u, 0x7f800000u, 0x7fc01234u, 0x3f800000u } )
        for( unsigned shift = 0; shift < 32; shift += 8 ) bytes.push_back( uint8_t( word >> shift ) );
    REQUIRE( TerrainData::EncodeStreamBlock( bytes, encoded, error ) ); CHECK( encoded.size() < bytes.size() );
    REQUIRE( TerrainData::DecodeStreamBlock( encoded, bytes.size(), encoded, error ) ); CHECK( encoded == bytes );
}


TEST_CASE( "Terrain compression: malformed envelopes and runs preserve output without untrusted allocation" )
{
    std::string error; const std::vector<uint8_t> original{ 7, 8, 9 }; std::vector<uint8_t> out = original;
    const auto reject = [&]( const std::vector<uint8_t>& bytes, uint64_t expected = 4 ) {
        CHECK_FALSE( TerrainData::DecodeStreamBlock( bytes, expected, out, error ) ); CHECK( out == original ); CHECK_FALSE( error.empty() );
    };
    reject( {} ); reject( { 0, 0, 0, 0, 4, 0, 0 } );
    reject( { 255, 0, 0, 0, 4, 0, 0, 0, 129, 0 } ); // unknown codec
    reject( { 3, 0, 0, 0, 4, 0, 0, 0, 129, 0 } ); // incompatible plane stride
    reject( { 1, 0, 0, 0, 4, 0, 0, 0, 255, 0 } ); // expanded output overrun
    reject( { 1, 0, 0, 0, 4, 0, 0, 0, 129 } ); // missing run value
    reject( { 1, 0, 0, 0, 4, 0, 0, 0, 2, 0 } ); // truncated literal
    reject( { 1, 0, 0, 0, 4, 0, 0, 0, 128, 0 } ); // decoded output underrun
    reject( { 0, 0, 0, 0, 4, 0, 0, 0, 0, 0, 0 } ); // short raw block
    reject( { 0, 0, 0, 0, 4, 0, 0, 0, 0, 0, 0, 0, 0 } ); // trailing bytes
    reject( { 1, 0, 0, 0, 0xff, 0xff, 0xff, 0x7f, 129, 0 } ); // envelope lies about decoded size
    reject( { 0, 0, 0, 0, 4, 0, 0, 0 }, TerrainData::MaxStreamBlockBytes + 4 );
    reject( { 0, 0, 0, 0, 4, 0, 0, 0 }, std::numeric_limits<uint64_t>::max() );
    reject( { 0, 0, 0, 0, 3, 0, 0, 0 }, 3 );
    CHECK_FALSE( TerrainData::EncodeStreamBlock( original, out, error ) ); CHECK( out == original );
}


TEST_CASE( "Terrain compression: height and painted archives exactly match legacy pages at every coverage level" )
{
    for( bool painted : { false, true } )
    {
        CompressedFixture f( painted ); std::string error;
        for( uint8_t level = 0; level <= 3; ++level )
        {
            REQUIRE( TerrainData::CookCompressedStreamArchive( f.File, f.Field, 12345, level, error ) ); REQUIRE( TerrainData::OpenStreamArchive( f.File, f.Archive, error ) );
            const auto legacyFile = f.Directory / "Legacy.terrainc"; std::shared_ptr<const TerrainData::StreamArchive> legacy;
            REQUIRE( TerrainData::CookPagedStreamArchive( legacyFile, f.Field, 12345, level, error ) ); REQUIRE( TerrainData::OpenStreamArchive( legacyFile, legacy, error ) );
            REQUIRE( f.Archive->Compressed ); REQUIRE( f.Archive->PagedCoverage ); CHECK_FALSE( legacy->Compressed );
            CHECK( TerrainData::IsStreamArchive( f.Archive->Header ) ); CHECK( f.Archive->Header[3] == '5' ); CHECK( f.Archive->Revision == legacy->Revision );
            CHECK( f.Archive->Coverage->Patches.empty() ); CHECK( f.Archive->Coverage->Heights->GetTiles().empty() );
            if( painted ) { REQUIRE( f.Archive->Coverage->Heights->GetSurface() ); CHECK( f.Archive->Coverage->Heights->GetSurface()->Layers == f.Field.GetSurface()->Layers ); CHECK( f.Archive->Coverage->Heights->GetSurface()->BaseWeights == f.Field.GetSurface()->BaseWeights ); }
            uint64_t stored = 0, decoded = 0;
            for( const auto& block : f.Archive->Blocks )
            {
                stored += block.Size + block.CoverageSize; decoded += block.DecodedSize + block.CoverageDecodedSize;
                CHECK( block.Size <= block.DecodedSize + 8 ); CHECK( block.CoverageSize <= block.CoverageDecodedSize + 8 );
                std::shared_ptr<const TerrainData::StreamCoverageTile> a, b;
                REQUIRE( TerrainData::ReadStreamCoverage( *f.Archive, block.Coordinate, a, error ) ); REQUIRE( TerrainData::ReadStreamCoverage( *legacy, block.Coordinate, b, error ) ); SamePatches( a->Patches, b->Patches );
                std::shared_ptr<const TerrainData::StreamTile> x, y;
                REQUIRE( TerrainData::ReadStreamTile( *f.Archive, block.Coordinate, x, error ) ); REQUIRE( TerrainData::ReadStreamTile( *legacy, block.Coordinate, y, error ) );
                CHECK( x->Heights == y->Heights ); CHECK( x->Heights == f.Field.FindTile( block.Coordinate )->Heights ); SamePatches( x->Patches, y->Patches );
                CHECK( x->Collision->Indices == y->Collision->Indices ); REQUIRE( x->Collision->Positions.size() == y->Collision->Positions.size() );
                for( size_t i = 0; i < x->Collision->Positions.size(); ++i ) CHECK( x->Collision->Positions[i].InternalVector == y->Collision->Positions[i].InternalVector );
                std::vector<float> heights; REQUIRE( TerrainData::ReadStreamHeights( *f.Archive, block.Coordinate, heights, error ) ); CHECK( heights == x->Heights );
            }
            CHECK( stored < decoded );
        }
    }
}


TEST_CASE( "Terrain compression: corrupt blocks stay isolated and metadata opens without reading pages" )
{
    CompressedFixture f( true ); std::string error; const auto block = f.Archive->Blocks.front(); const auto coord = block.Coordinate;
    std::shared_ptr<const TerrainData::StreamCoverageTile> page; REQUIRE( TerrainData::ReadStreamCoverage( *f.Archive, coord, page, error ) ); const auto preserved = page;
    Byte( f.File, block.CoverageOffset, 255 );
    std::shared_ptr<const TerrainData::StreamArchive> reopened; REQUIRE( TerrainData::OpenStreamArchive( f.File, reopened, error ) ); CHECK( reopened->Coverage->Patches.empty() );
    CHECK_FALSE( TerrainData::ReadStreamCoverage( *reopened, coord, page, error ) ); CHECK( page == preserved );
    std::vector<float> heights; REQUIRE( TerrainData::ReadStreamHeights( *reopened, coord, heights, error ) ); CHECK( heights == f.Field.FindTile( coord )->Heights );
    std::ifstream payload( f.File, std::ios::binary ); payload.seekg( std::streamoff( block.Offset + block.Size - 1 ) ); const uint8_t last = uint8_t( payload.get() ); payload.close();
    Byte( f.File, block.Offset + block.Size - 1, last ^ 1 );
    const auto previous = heights; CHECK_FALSE( TerrainData::ReadStreamHeights( *reopened, coord, heights, error ) ); CHECK( heights == previous ); CHECK( error.find( "checksum" ) != std::string::npos );
    Byte( f.File, block.Offset + block.Size - 1, last );
    Byte( f.File, block.Offset + 7, 255 ); // impossible decoded length, rejected before allocation
    CHECK_FALSE( TerrainData::ReadStreamHeights( *reopened, coord, heights, error ) ); CHECK( heights == previous );
    const auto other = f.Archive->Blocks.back().Coordinate; std::shared_ptr<const TerrainData::StreamTile> tile;
    REQUIRE( TerrainData::ReadStreamTile( *reopened, other, tile, error ) ); REQUIRE( TerrainData::ReadStreamCoverage( *reopened, other, page, error ) );
    CHECK( tile->Heights == f.Field.FindTile( other )->Heights );
}


TEST_CASE( "Terrain compression: failed publication and obsolete archive generations preserve valid outputs" )
{
    CompressedFixture f; std::string error; const auto oldSize = std::filesystem::file_size( f.File ); const auto old = f.Archive;
    auto incomplete = f.Field; incomplete.UnloadTile( { -1, -1 } );
    CHECK_FALSE( TerrainData::CookCompressedStreamArchive( f.File, incomplete, 12345, 2, error ) );
    CHECK( std::filesystem::file_size( f.File ) == oldSize ); REQUIRE( TerrainData::OpenStreamArchive( f.File, f.Archive, error ) ); CHECK( f.Archive->Header == old->Header );
    for( const auto& entry : std::filesystem::directory_iterator( f.Directory ) ) CHECK( entry.path().extension() != ".tmp" );
    REQUIRE( f.Field.SetSample( { 0, 0 }, 4.f, error ) ); REQUIRE( TerrainData::CookCompressedStreamArchive( f.File, f.Field, 12345, 2, error ) );
    REQUIRE( TerrainData::OpenStreamArchive( f.File, f.Archive, error ) ); CHECK( f.Archive->Header != old->Header );
    std::vector<float> heights{ 99 }; CHECK_FALSE( TerrainData::ReadStreamHeights( *old, { 0, 0 }, heights, error ) ); CHECK( heights == std::vector<float>{ 99 } );
    std::ifstream input( f.File, std::ios::binary ); const std::vector<uint8_t> valid( ( std::istreambuf_iterator<char>( input ) ), {} ); input.close();
    const auto preserved = f.Archive;
    // Valid checksums must not bypass stored-size or contiguous-range validation.
    for( const auto [offset, value] : std::initializer_list<std::pair<size_t, uint64_t>>{
        { 104 + 16, UINT64_MAX }, { 104 + 48, UINT64_MAX }, { 104 + 48, 7 }, { 104 + 8, f.Archive->Blocks.front().Offset + 1 } } )
    {
        auto bytes = valid; Put64( bytes, offset, value );
        Put64( bytes, 88, Checksum( std::span( bytes ).subspan( 104, f.Archive->Blocks.size() * 64 ) ) );
        Put64( bytes, 96, Checksum( std::span( bytes ).first( 96 ) ) );
        { std::ofstream output( f.File, std::ios::binary | std::ios::trunc ); output.write( reinterpret_cast<const char*>( bytes.data() ), bytes.size() ); REQUIRE( output.good() ); }
        CHECK_FALSE( TerrainData::OpenStreamArchive( f.File, f.Archive, error ) ); CHECK( f.Archive == preserved );
    }
    { std::ofstream output( f.File, std::ios::binary | std::ios::trunc ); output.write( reinterpret_cast<const char*>( valid.data() ), valid.size() - 1 ); }
    CHECK_FALSE( TerrainData::OpenStreamArchive( f.File, f.Archive, error ) ); CHECK( f.Archive == preserved );
}


TEST_CASE( "Terrain compression: cache charges decoded scratch and retains pinned reservations during promotion and eviction" )
{
    CompressedFixture f( true ); TerrainData::TerrainStreamCache cache( f.Archive ); const auto& s = f.Field.GetSettings();
    const auto raw = TerrainData::TerrainStreamCache::TileReservation( s ), packed = TerrainData::TerrainStreamCache::TileReservation( s, true );
    CHECK( packed.CpuBytes == raw.CpuBytes + s.SamplesPerTile() * 16 + 8 ); CHECK( packed.GpuBytes == raw.GpuBytes ); CHECK( packed.CollisionBytes == raw.CollisionBytes );
    CHECK( TerrainData::TerrainStreamCache::CoverageReservation( s, 2, true, true ).CpuBytes > TerrainData::TerrainStreamCache::CoverageReservation( s, 2, true ).CpuBytes );
    const auto base = cache.GetStats().CpuReserved; TerrainData::StreamLimits limits; limits.HoldFrames = 0; uint64_t frame = 0;
    TerrainData::TileDemand demand{ { -1, -1 }, false, false, 0, false, true }; DrainCompressed( cache, { &demand, 1 }, limits, frame );
    REQUIRE( cache.GetStats().Resident == 1 ); CHECK( cache.GetStats().PrefetchResident == 1 ); CHECK( cache.GetStats().Detail == 0 ); CHECK( cache.GetStats().CoverageResident == 0 );
    const auto& block = f.Archive->Blocks.front(); CHECK( cache.GetStats().BytesRead == block.Size ); CHECK( cache.GetStats().DecodedBytesRead == block.DecodedSize );
    demand.Collision = demand.Visual = true; demand.Prefetch = false; DrainCompressed( cache, { &demand, 1 }, limits, frame );
    REQUIRE( cache.GetStats().CollisionReady ); CHECK( cache.GetStats().Loaded == 1 ); CHECK( cache.GetStats().CoverageLoaded == 1 ); REQUIRE( cache.GetData()->CollisionTiles.size() == 1 );
    CHECK( cache.GetStats().BytesRead == block.Size + block.CoverageSize ); CHECK( cache.GetStats().DecodedBytesRead == block.DecodedSize + block.CoverageDecodedSize );
    CHECK( cache.GetStats().BytesRead < cache.GetStats().DecodedBytesRead ); CHECK( cache.GetStats().CpuReserved <= limits.CpuBytes );
    auto pin = cache.GetData(); DrainCompressed( cache, {}, limits, frame ); CHECK_FALSE( cache.CanRetire() ); CHECK( cache.GetStats().Retiring == 1 ); CHECK( cache.GetStats().CoverageRetiring == 1 ); CHECK( cache.GetStats().CpuReserved >= base + packed.CpuBytes );
    CHECK( pin->Heights->Query( -8, -8 ).Status == TerrainData::QueryStatus::Ready ); pin.reset(); DrainCompressed( cache, {}, limits, frame ); CHECK( cache.CanRetire() ); CHECK( cache.GetStats().CpuReserved == base );
}
