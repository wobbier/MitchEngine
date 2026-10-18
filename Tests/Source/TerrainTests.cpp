#include <doctest/doctest.h>

#include "Terrain/Heightfield.h"
#include "Terrain/TerrainMesh.h"
#include "Terrain/TerrainSerialization.h"
#include "JSON.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <functional>
#include <limits>
#include <stdexcept>

namespace TerrainTest
{
    using namespace TerrainData;

    Settings SmallSettings()
    {
        Settings s;
        s.TileQuads = 4; s.PatchQuads = 2; s.MinTile = { -1, -1 }; s.TilesX = s.TilesZ = 2;
        return s;
    }

    Heightfield MakeField( const Settings& s, const std::function<float( int64_t, int64_t )>& height = []( int64_t, int64_t ) { return 0.f; } )
    {
        Heightfield field( s );
        std::string error;
        for( uint32_t x = 0; x < s.TilesX; ++x )
            for( uint32_t z = 0; z < s.TilesZ; ++z )
            {
                TileCoord tile{ static_cast<int32_t>( static_cast<int64_t>( s.MinTile.X ) + x ), static_cast<int32_t>( static_cast<int64_t>( s.MinTile.Z ) + z ) };
                std::vector<float> heights;
                for( uint32_t iz = 0; iz <= s.TileQuads; ++iz )
                    for( uint32_t ix = 0; ix <= s.TileQuads; ++ix )
                        heights.push_back( height( static_cast<int64_t>( tile.X ) * s.TileQuads + ix, static_cast<int64_t>( tile.Z ) * s.TileQuads + iz ) );
                REQUIRE( field.LoadTile( tile, std::move( heights ), error ) );
            }
        field.ClearDirty();
        return field;
    }

    Manifest MakeManifest( const Settings& s )
    {
        Manifest m; m.AssetGUID = 0x0123456789abcdefull; m.Config = s;
        for( uint32_t x = 0; x < s.TilesX; ++x )
            for( uint32_t z = 0; z < s.TilesZ; ++z )
            {
                TileCoord tile{ static_cast<int32_t>( static_cast<int64_t>( s.MinTile.X ) + x ), static_cast<int32_t>( static_cast<int64_t>( s.MinTile.Z ) + z ) };
                m.Pages.push_back( { tile, "Terrain.source/" + std::to_string( tile.X ) + "_" + std::to_string( tile.Z ) + ".thp" } );
            }
        return m;
    }

    uint64_t Hash( std::span<const uint8_t> bytes )
    {
        uint64_t h = 14695981039346656037ull;
        for( uint8_t b : bytes ) h = ( h ^ b ) * 1099511628211ull;
        return h;
    }

    void Put64( std::vector<uint8_t>& bytes, size_t offset, uint64_t value )
    {
        for( unsigned i = 0; i < 8; ++i ) bytes.at( offset + i ) = static_cast<uint8_t>( value >> ( 8 * i ) );
    }

    void Put32( std::vector<uint8_t>& bytes, size_t offset, uint32_t value )
    {
        for( unsigned i = 0; i < 4; ++i ) bytes.at( offset + i ) = static_cast<uint8_t>( value >> ( 8 * i ) );
    }

    void RehashCook( std::vector<uint8_t>& bytes, size_t count )
    {
        Put64( bytes, 72, Hash( std::span<const uint8_t>( bytes ).subspan( 88, count * 40 ) ) );
        Put64( bytes, 80, Hash( std::span<const uint8_t>( bytes ).first( 80 ) ) );
    }
}

using namespace TerrainData;
using namespace TerrainTest;

TEST_CASE( "Terrain: settings validate dimensions, allocation limits, and coordinate extent" )
{
    std::string error;
    Settings s;
    CHECK( s.Validate( error ) );
    CHECK( s.SamplesPerTile() == 257 * 257 );
    s.TileQuads = 3; CHECK_FALSE( s.Validate( error ) );
    s = {}; s.PatchQuads = 512; CHECK_FALSE( s.Validate( error ) );
    s = {}; s.TileQuads = 8192; CHECK_FALSE( s.Validate( error ) );
    s = {}; s.SampleSpacing = 0.f; CHECK_FALSE( s.Validate( error ) );
    CHECK_THROWS_AS( Heightfield{ s }, std::invalid_argument );
    s = {}; s.SampleSpacing = std::numeric_limits<float>::infinity(); CHECK_FALSE( s.Validate( error ) );
    s = {}; s.HeightRange = -1.f; CHECK_FALSE( s.Validate( error ) );
    s = {}; s.HeightBase = std::numeric_limits<float>::quiet_NaN(); CHECK_FALSE( s.Validate( error ) );
    s = {}; s.TilesZ = 0; CHECK_FALSE( s.Validate( error ) );
    s = {}; s.MinTile.X = INT32_MAX; s.TilesX = 2; CHECK_FALSE( s.Validate( error ) );
    s.TilesX = 1; CHECK( s.Validate( error ) );
    CHECK( s.Owner( s.MaxSample() ) == TileCoord{ INT32_MAX, 0 } );
}

TEST_CASE( "Terrain: signed addressing uses floor division and clamps only the outer owner" )
{
    CHECK( FloorDivide( -1, 4 ) == -1 ); CHECK( FloorDivide( -4, 4 ) == -1 );
    CHECK( FloorDivide( -5, 4 ) == -2 ); CHECK( FloorDivide( 5, 4 ) == 1 );
    CHECK( FloorDivide( INT64_MIN, 1 ) == INT64_MIN );
    CHECK_THROWS_AS( FloorDivide( 1, 0 ), std::invalid_argument );
    const auto s = SmallSettings();
    CHECK( s.Owner( { -1, -1 } ) == TileCoord{ -1, -1 } );
    CHECK( s.Owner( { 0, 0 } ) == TileCoord{ 0, 0 } );
    CHECK( s.Owner( { -4, 4 } ) == TileCoord{ -1, 0 } );
    CHECK( s.Owner( { 4, 4 } ) == TileCoord{ 0, 0 } );
    CHECK_THROWS_AS( s.Owner( { -5, 0 } ), std::out_of_range );
}

TEST_CASE( "Terrain: a shared corner edit synchronizes four pages as one revision" )
{
    auto field = MakeField( SmallSettings() );
    std::string error;
    const uint64_t previous = field.GetRevision();
    REQUIRE( field.SetSample( { 0, 0 }, 7.f, error ) );
    CHECK( field.GetRevision() == previous + 1 );
    for( const auto& [coord, tile] : field.GetTiles() )
    {
        const size_t x = coord.X == -1 ? 4 : 0, z = coord.Z == -1 ? 4 : 0;
        CHECK( tile.Heights[z * 5 + x] == 7.f );
        CHECK( tile.Revision == field.GetRevision() );
        CHECK_FALSE( tile.DirtySamples.IsEmpty() );
        CHECK( tile.DirtySamples.Min.X == ( coord.X == -1 ? 3 : 0 ) );
        CHECK( tile.DirtySamples.Max.X == ( coord.X == -1 ? 4 : 1 ) );
    }
    REQUIRE( field.SetSample( { 0, 0 }, 7.f, error ) );
    CHECK( field.GetRevision() == previous + 1 );
    field.ClearDirty();
    for( const auto& [coord, tile] : field.GetTiles() ) CHECK( tile.DirtySamples.IsEmpty() );
    REQUIRE( field.SetSample( { 4, 4 }, 3.f, error ) );
    float height;
    CHECK( field.GetSample( { 4, 4 }, height ) == QueryStatus::Ready ); CHECK( height == 3.f );
}

TEST_CASE( "Terrain: edits require every shared source page and missing data is explicit" )
{
    Heightfield field( SmallSettings() );
    std::string error;
    REQUIRE( field.CreateFlatTile( { -1, -1 }, 2.f, error ) );
    const auto previous = field.GetRevision();
    CHECK_FALSE( field.SetSample( { 0, 0 }, 9.f, error ) );
    CHECK( field.GetRevision() == previous );
    CHECK( field.FindTile( { -1, -1 } )->Heights.back() == 2.f );
    float height = 123.f;
    CHECK( field.GetSample( { 0, 0 }, height ) == QueryStatus::Unavailable );
    CHECK( height == 123.f );
    REQUIRE( field.SetPending( { 0, 0 }, true ) );
    CHECK( field.GetSample( { 0, 0 }, height ) == QueryStatus::Pending );
    CHECK( field.Query( 0.25, 0.25 ).Status == QueryStatus::Pending );
    CHECK_FALSE( field.SetPending( { -1, -1 }, true ) );
    REQUIRE( field.CreateFlatTile( { 0, 0 }, 2.f, error ) );
    CHECK( field.Query( 0.25, 0.25 ).Status == QueryStatus::Ready );
    field.UnloadTile( { 0, 0 } );
    CHECK( field.Query( 0.25, 0.25 ).Status == QueryStatus::Unavailable );
}

TEST_CASE( "Terrain: tile loads reject invalid data and disagreeing shared borders atomically" )
{
    Heightfield field( SmallSettings() );
    std::string error;
    REQUIRE( field.CreateFlatTile( { -1, -1 }, 1.f, error ) );
    const auto revision = field.GetRevision();
    CHECK_FALSE( field.LoadTile( { 0, -1 }, std::vector<float>( 25, 2.f ), error ) );
    CHECK( field.FindTile( { 0, -1 } ) == nullptr ); CHECK( field.GetRevision() == revision );
    CHECK_FALSE( field.LoadTile( { 0, -1 }, std::vector<float>( 24, 1.f ), error ) );
    CHECK_FALSE( field.CreateFlatTile( { 2, 0 }, 1.f, error ) );
    CHECK_FALSE( field.CreateFlatTile( { 0, 0 }, std::numeric_limits<float>::quiet_NaN(), error ) );
    REQUIRE( field.CreateFlatTile( { 0, -1 }, 1.f, error ) );
    CHECK_FALSE( field.CreateFlatTile( { -1, -1 }, 2.f, error ) );
    CHECK( field.FindTile( { -1, -1 } )->Heights.front() == 1.f );
    CHECK_FALSE( field.SetSample( { -1, -1 }, std::numeric_limits<float>::infinity(), error ) );
}

TEST_CASE( "Terrain: normals share neighbor halos and edits invalidate the adjacent normal strip" )
{
    auto field = MakeField( SmallSettings(), []( int64_t x, int64_t z ) { return static_cast<float>( 2 * x + 3 * z ); } );
    Vector3 n;
    for( SampleCoord sample : { SampleCoord{ 0, 0 }, { -4, -4 }, { 4, 4 }, { 0, 1 } } )
    {
        REQUIRE( field.GetNormal( sample, n ) == QueryStatus::Ready );
        CHECK( n.x == doctest::Approx( -2.0 / std::sqrt( 14.0 ) ) );
        CHECK( n.y == doctest::Approx( 1.0 / std::sqrt( 14.0 ) ) );
        CHECK( n.z == doctest::Approx( -3.0 / std::sqrt( 14.0 ) ) );
    }
    std::string error;
    REQUIRE( field.SetSample( { -1, 2 }, 20.f, error ) );
    const auto* neighbor = field.FindTile( { 0, 0 } );
    REQUIRE( neighbor != nullptr );
    CHECK( neighbor->DirtySamples.Min.X == 0 ); CHECK( neighbor->DirtySamples.Max.X == 0 );
    CHECK( neighbor->DirtySamples.Min.Z == 1 ); CHECK( neighbor->DirtySamples.Max.Z == 3 );
    CHECK( neighbor->Revision == field.GetRevision() );
    REQUIRE( field.GetNormal( { 0, 2 }, n ) == QueryStatus::Ready ); CHECK( n.x > 0.f );
    field.UnloadTile( { -1, 0 } );
    CHECK( field.GetNormal( { 0, 2 }, n ) == QueryStatus::Unavailable );
}

TEST_CASE( "Terrain: height queries follow the triangle diagonal, not a bilinear saddle" )
{
    Settings s; s.TileQuads = s.PatchQuads = 1;
    Heightfield field( s );
    std::string error;
    REQUIRE( field.LoadTile( { 0, 0 }, { 0.f, 0.f, 0.f, 10.f }, error ) );
    auto query = field.Query( 0.25, 0.25 );
    REQUIRE( query.Status == QueryStatus::Ready ); CHECK( query.Height == 0.f ); CHECK( query.Normal.y == 1.f );
    query = field.Query( 0.75, 0.75 );
    CHECK( query.Height == doctest::Approx( 5.f ) );
    CHECK( query.Normal.x == doctest::Approx( -10.0 / std::sqrt( 201.0 ) ) );
    CHECK( field.Query( 1.0, 1.0 ).Height == 10.f );
    CHECK( field.Query( 0.5, 0.5 ).Height == 0.f );
    CHECK( field.Query( -0.01, 0.0 ).Status == QueryStatus::OutsideBounds );
    CHECK( field.Query( 1.01, 0.0 ).Status == QueryStatus::OutsideBounds );
    CHECK( field.Query( std::numeric_limits<double>::quiet_NaN(), 0.0 ).Status == QueryStatus::InvalidInput );
    CHECK( field.Query( std::numeric_limits<double>::infinity(), 0.0 ).Status == QueryStatus::InvalidInput );
}

TEST_CASE( "Terrain: extreme signed tile coordinates retain integer sample identity" )
{
    Settings s; s.TileQuads = s.PatchQuads = 2; s.MinTile = { INT32_MAX, INT32_MIN }; s.SampleSpacing = 0.5f;
    auto field = MakeField( s, []( int64_t, int64_t ) { return 3.f; } );
    CHECK( field.Query( s.MaxSample().X * 0.5, s.MinSample().Z * 0.5 ).Height == 3.f );
    PatchMesh mesh;
    std::string error;
    REQUIRE( BuildPatch( field, s.MinTile, 0, 0, mesh, error ) );
    CHECK( mesh.SampleOrigin == SampleCoord{ int64_t( INT32_MAX ) * 2, int64_t( INT32_MIN ) * 2 } );
    CHECK( mesh.Vertices.front().Position.x == 0.f ); CHECK( mesh.Bounds.Max.x == 1.f );
}

TEST_CASE( "Terrain: patch meshes have nested upward triangles, bounds, and conservative errors" )
{
    Settings s; s.TileQuads = s.PatchQuads = 4;
    auto field = MakeField( s, []( int64_t x, int64_t z ) { return x == 2 && z == 2 ? 4.f : 0.f; } );
    PatchMesh mesh;
    std::string error;
    REQUIRE( BuildPatch( field, { 0, 0 }, 0, 0, mesh, error ) );
    CHECK( mesh.Vertices.size() == 25 ); REQUIRE( mesh.Lods.size() == 3 );
    CHECK( mesh.Lods[0].Indices.size() == 96 ); CHECK( mesh.Lods[2].Indices.size() == 6 );
    CHECK( mesh.Lods[0].MaxError == 0.f ); CHECK( mesh.Lods[2].MaxError == 4.f );
    CHECK( mesh.Bounds.Max.y == 4.f ); CHECK( mesh.Bounds.Min.y == 0.f );
    CHECK( mesh.Vertices.back().U == 1.f ); CHECK( mesh.Vertices.back().V == 1.f );
    for( const auto& lod : mesh.Lods )
    {
        for( size_t i = 0; i < lod.Indices.size(); i += 3 )
        {
            REQUIRE( lod.Indices[i] < mesh.Vertices.size() ); REQUIRE( lod.Indices[i + 1] < mesh.Vertices.size() ); REQUIRE( lod.Indices[i + 2] < mesh.Vertices.size() );
            const auto a = mesh.Vertices[lod.Indices[i]].Position;
            const auto b = mesh.Vertices[lod.Indices[i + 1]].Position - a;
            const auto c = mesh.Vertices[lod.Indices[i + 2]].Position - a;
            CHECK( b.z * c.x - b.x * c.z > 0.f );
        }
        // Sample triangle interiors as well as vertices, independently of the builder's error loop.
        const auto h = [&]( int x, int z ) { return mesh.Vertices[z * 5 + x].Position.y; };
        for( int iz = 0; iz <= 16; ++iz ) for( int ix = 0; ix <= 16; ++ix )
        {
            const double x = ix * 0.25, z = iz * 0.25;
            const int cx = std::min( static_cast<int>( x ) / static_cast<int>( lod.Stride ) * static_cast<int>( lod.Stride ), 4 - static_cast<int>( lod.Stride ) );
            const int cz = std::min( static_cast<int>( z ) / static_cast<int>( lod.Stride ) * static_cast<int>( lod.Stride ), 4 - static_cast<int>( lod.Stride ) );
            const double u = ( x - cx ) / lod.Stride, v = ( z - cz ) / lod.Stride;
            const double a = h( cx, cz ), b = h( cx + lod.Stride, cz ), c = h( cx, cz + lod.Stride ), d = h( cx + lod.Stride, cz + lod.Stride );
            const double coarse = u + v <= 1.0 ? a + ( b - a ) * u + ( c - a ) * v : d + ( c - d ) * ( 1.0 - u ) + ( b - d ) * ( 1.0 - v );
            CHECK( std::abs( field.Query( x, z ).Height - coarse ) <= lod.MaxError + 1e-6 );
        }
    }
    const auto previous = mesh.Vertices.size();
    CHECK_FALSE( BuildPatch( field, { 0, 0 }, 1, 0, mesh, error ) ); CHECK( mesh.Vertices.size() == previous );
}

TEST_CASE( "Terrain: patch snapshots retain revisions and agree on cross-tile normals" )
{
    auto field = MakeField( SmallSettings(), []( int64_t x, int64_t z ) { return static_cast<float>( x + z ); } );
    const auto snapshot = field;
    std::string error;
    REQUIRE( field.SetSample( { 0, 0 }, 9.f, error ) );
    PatchMesh left, right;
    REQUIRE( BuildPatch( snapshot, { -1, 0 }, 1, 0, left, error ) );
    REQUIRE( BuildPatch( snapshot, { 0, 0 }, 0, 0, right, error ) );
    CHECK( left.Revision < field.GetRevision() );
    CHECK( left.Vertices[2].Position.y == right.Vertices[0].Position.y );
    CHECK( left.Vertices[2].Normal.x == right.Vertices[0].Normal.x );
    CHECK( left.SampleOrigin == SampleCoord{ -2, 0 } );
    CHECK( left.Bounds.Min.x == 0.f ); CHECK( left.Bounds.Max.x == 2.f );
    field.UnloadTile( { 0, 0 } );
    CHECK_FALSE( BuildPatch( field, { -1, 0 }, 1, 0, right, error ) );
}

TEST_CASE( "Terrain: manifest and source pages round trip a signed two-by-two dataset" )
{
    const auto s = SmallSettings();
    auto field = MakeField( s, []( int64_t x, int64_t z ) { return static_cast<float>( x * x - z ) * 0.03125f; } );
    auto manifest = MakeManifest( s );
    std::string text, error;
    REQUIRE( SerializeManifest( manifest, text, error ) );
    std::reverse( manifest.Pages.begin(), manifest.Pages.end() );
    std::string reordered;
    REQUIRE( SerializeManifest( manifest, reordered, error ) ); CHECK( reordered == text );
    Manifest restored;
    REQUIRE( DeserializeManifest( text, restored, error ) ); CHECK( restored.AssetGUID == manifest.AssetGUID );
    REQUIRE( restored.Pages.size() == 4 ); CHECK( restored.Config.MinTile == s.MinTile );
    Heightfield loaded( restored.Config );
    for( const auto& ref : restored.Pages )
    {
        std::vector<uint8_t> bytes;
        REQUIRE( SerializePage( field, ref.Coordinate, bytes, error ) );
        CHECK( bytes[0] == 'M' ); CHECK( bytes[1] == 'T' ); CHECK( bytes[2] == 'H' ); CHECK( bytes[3] == 'P' );
        TilePage page;
        REQUIRE( DeserializePage( bytes, restored.Config, page, error ) );
        CHECK( page.Coordinate == ref.Coordinate ); CHECK( page.Heights == field.FindTile( ref.Coordinate )->Heights );
        REQUIRE( loaded.LoadTile( page.Coordinate, std::move( page.Heights ), error ) );
    }
    CHECK( loaded.Query( -0.5, 0.5 ).Height == field.Query( -0.5, 0.5 ).Height );
}

TEST_CASE( "Terrain: manifests reject invalid versions, coordinates, numbers, and unsafe paths" )
{
    std::string text, error;
    REQUIRE( SerializeManifest( MakeManifest( SmallSettings() ), text, error ) );
    const json valid = json::parse( text );
    Manifest output; output.AssetGUID = 42;
    const auto reject = [&]( const json& j ) { CHECK_FALSE( DeserializeManifest( j.dump(), output, error ) ); CHECK( output.AssetGUID == 42 ); CHECK_FALSE( error.empty() ); };
    auto j = valid; j["Version"] = 2; reject( j );
    j = valid; j["AssetGUID"] = "0000000000000000"; reject( j );
    j = valid; j["AssetGUID"] = "00000000000000gg"; reject( j );
    j = valid; j["Settings"]["TileQuads"] = 4.5; reject( j );
    j = valid; j["Settings"]["TilesX"] = UINT64_MAX; reject( j );
    j = valid; j["Settings"]["TilesZ"] = 0; reject( j );
    j = valid; j["Settings"]["MinTile"][0] = INT64_MIN; reject( j );
    j = valid; j["Settings"]["SampleSpacing"] = -1; reject( j );
    j = valid; j["Pages"][1]["Tile"] = j["Pages"][0]["Tile"]; reject( j );
    j = valid; j["Pages"].erase( 0 ); reject( j );
    for( const std::string path : { "../outside.thp", "/absolute.thp", "C:/terrain.thp", "folder\\page.thp", "folder//page.thp", "./page.thp", "folder/../page.thp", "" } )
    { j = valid; j["Pages"][0]["Source"] = path; reject( j ); }
    j = valid; j["Pages"][1]["Source"] = j["Pages"][0]["Source"]; reject( j );
    CHECK_FALSE( DeserializeManifest( "{", output, error ) );
}

TEST_CASE( "Terrain: source page decoders reject truncation, corruption, and non-finite payloads" )
{
    auto field = MakeField( SmallSettings() );
    std::vector<uint8_t> bytes;
    std::string error;
    REQUIRE( SerializePage( field, { -1, -1 }, bytes, error ) );
    TilePage output; output.Coordinate = { 77, 88 }; output.Heights = { 123.f };
    for( size_t length = 0; length < bytes.size(); ++length )
    {
        CHECK_FALSE( DeserializePage( std::span<const uint8_t>( bytes ).first( length ), field.GetSettings(), output, error ) );
        CHECK( output.Coordinate == TileCoord{ 77, 88 } ); CHECK( output.Heights == std::vector<float>{ 123.f } );
    }
    auto bad = bytes; bad.back() ^= 1;
    CHECK_FALSE( DeserializePage( bad, field.GetSettings(), output, error ) );
    bad = bytes; bad.push_back( 0 ); CHECK_FALSE( DeserializePage( bad, field.GetSettings(), output, error ) );
    bad = bytes; Put32( bad, 36, 0x7fc00000 ); Put64( bad, 28, Hash( std::span<const uint8_t>( bad ).subspan( 36 ) ) );
    CHECK_FALSE( DeserializePage( bad, field.GetSettings(), output, error ) );
    auto other = field.GetSettings(); other.TileQuads = 8;
    CHECK_FALSE( DeserializePage( bytes, other, output, error ) );
}

TEST_CASE( "Terrain: Float32 and UInt16 cooks are deterministic and preserve shared borders" )
{
    const auto field = MakeField( SmallSettings(), []( int64_t x, int64_t z ) { return static_cast<float>( x * x + z * 3 ) * 0.123f; } );
    std::string error;
    uint64_t previousHash = 0;
    for( auto encoding : { HeightEncoding::Float32, HeightEncoding::UInt16 } )
    {
        std::vector<uint8_t> bytes, repeat;
        REQUIRE( CookHeightfield( field, 123, encoding, bytes, error ) );
        REQUIRE( CookHeightfield( field, 123, encoding, repeat, error ) ); CHECK( bytes == repeat );
        CHECK( bytes[0] == 'M' ); CHECK( bytes[1] == 'T' ); CHECK( bytes[2] == 'R' ); CHECK( bytes[3] == 'N' );
        Heightfield loaded;
        CookInfo info;
        REQUIRE( ReadCookedHeightfield( bytes, loaded, info, error ) );
        CHECK( info.AssetGUID == 123 ); CHECK( info.Encoding == encoding ); CHECK( info.SourceHash != 0 );
        CHECK( loaded.GetSettings().MinTile == field.GetSettings().MinTile );
        for( const auto& [coord, tile] : field.GetTiles() )
        {
            const auto* actual = loaded.FindTile( coord ); REQUIRE( actual != nullptr );
            CHECK( actual->DirtySamples.IsEmpty() ); REQUIRE( actual->Heights.size() == tile.Heights.size() );
            for( size_t i = 0; i < tile.Heights.size(); ++i ) CHECK( std::abs( static_cast<double>( actual->Heights[i] ) - tile.Heights[i] ) <= info.QuantizationErrorBound );
        }
        if( encoding == HeightEncoding::Float32 ) { CHECK( info.QuantizationErrorBound == 0.0 ); previousHash = info.SourceHash; }
        else { CHECK( info.QuantizationErrorBound > 0.0 ); CHECK( info.SourceHash != previousHash ); }
    }
}

TEST_CASE( "Terrain: cooking rejects incomplete data and UInt16 range violations without clamping" )
{
    auto field = MakeField( SmallSettings() );
    std::string error;
    REQUIRE( field.SetSample( { 1, 1 }, 513.f, error ) );
    std::vector<uint8_t> bytes{ 7, 8, 9 };
    CHECK_FALSE( CookHeightfield( field, 1, HeightEncoding::UInt16, bytes, error ) );
    CHECK( bytes == std::vector<uint8_t>{ 7, 8, 9 } );
    REQUIRE( CookHeightfield( field, 1, HeightEncoding::Float32, bytes, error ) );
    field.UnloadTile( { 0, 0 } );
    CHECK_FALSE( CookHeightfield( field, 1, HeightEncoding::Float32, bytes, error ) );
    CHECK_FALSE( CookHeightfield( field, 0, HeightEncoding::Float32, bytes, error ) );
}

TEST_CASE( "Terrain: cooked readers validate lengths, checksums, directory offsets, and bounds" )
{
    auto field = MakeField( SmallSettings() );
    std::string error;
    std::vector<uint8_t> bytes;
    REQUIRE( CookHeightfield( field, 1, HeightEncoding::Float32, bytes, error ) );
    Heightfield output; CookInfo info; info.AssetGUID = 99;
    const auto reject = [&]( std::span<const uint8_t> bad ) {
        CHECK_FALSE( ReadCookedHeightfield( bad, output, info, error ) );
        CHECK( output.GetTiles().empty() ); CHECK( info.AssetGUID == 99 ); CHECK_FALSE( error.empty() );
    };
    for( size_t length = 0; length < bytes.size(); ++length ) reject( std::span<const uint8_t>( bytes ).first( length ) );
    auto bad = bytes; bad.back() ^= 1; reject( bad );
    bad = bytes; bad.push_back( 0 ); reject( bad );
    bad = bytes; bad[20] ^= 1; reject( bad );
    bad = bytes; Put64( bad, 88 + 8, UINT64_MAX ); RehashCook( bad, 4 ); reject( bad );
    bad = bytes; Put64( bad, 88 + 16, UINT64_MAX ); RehashCook( bad, 4 ); reject( bad );
    bad = bytes; Put32( bad, 88, 5 ); RehashCook( bad, 4 ); reject( bad );
    bad = bytes; Put32( bad, 88 + 32, 0x7fc00000 ); RehashCook( bad, 4 ); reject( bad );
    bad = bytes; Put64( bad, 56, UINT64_MAX ); Put64( bad, 80, Hash( std::span<const uint8_t>( bad ).first( 80 ) ) ); reject( bad );
    bad = bytes; Put32( bad, 8, 42 ); Put64( bad, 80, Hash( std::span<const uint8_t>( bad ).first( 80 ) ) ); reject( bad );
}
