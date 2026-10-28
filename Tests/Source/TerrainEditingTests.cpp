#include <doctest/doctest.h>
#include "Terrain/TerrainEditing.h"
#include "Terrain/TerrainAsset.h"
#include "Utils/GUID.h"
#include <filesystem>
#include <fstream>
#include <limits>
#include <set>

namespace TerrainEditingTests
{
    using namespace TerrainData;
    Heightfield Flat( float height = 0 )
    {
        Settings s; s.TileQuads = 8; s.PatchQuads = 4; s.MinTile = { -1, -1 }; s.TilesX = s.TilesZ = 2;
        Heightfield field( s ); std::string error;
        for( int z = -1; z <= 0; ++z ) for( int x = -1; x <= 0; ++x ) REQUIRE( field.CreateFlatTile( { x, z }, height, error ) );
        return field;
    }
    void Equal( const Heightfield& a, const Heightfield& b )
    {
        REQUIRE( a.GetTiles().size() == b.GetTiles().size() );
        for( const auto& [coord, tile] : a.GetTiles() ) { REQUIRE( b.FindTile( coord ) ); CHECK( tile.Heights == b.FindTile( coord )->Heights ); }
    }
    struct Files
    {
        std::filesystem::path Directory = std::filesystem::path( ".tmp/Tests/TerrainAuthoring" ) / std::to_string( Guid::Generate() );
        Files() { std::filesystem::create_directories( Directory ); }
        ~Files() { std::error_code ignored; std::filesystem::remove_all( Directory, ignored ); }
    };
    std::string Text( const std::filesystem::path& path )
    {
        std::ifstream file( path, std::ios::binary ); return { std::istreambuf_iterator<char>( file ), std::istreambuf_iterator<char>() };
    }
}
using namespace TerrainEditingTests;

TEST_CASE( "Terrain: one sculpt stroke spans four tiles and replays exact sample deltas" )
{
    auto field = Flat(); const auto before = field;
    BrushStroke stroke; BrushSettings brush; brush.Radius = 3; brush.Strength = 0.7f;
    std::string error;
    REQUIRE( stroke.Begin( field, brush, -2, -2, error ) );
    REQUIRE( stroke.Move( field, 2, 2, error ) );
    auto deltas = stroke.Finish(); REQUIRE_FALSE( deltas.empty() );
    const auto after = field;
    std::set<std::pair<int64_t, int64_t>> coordinates;
    for( const auto& delta : deltas )
    {
        CHECK( coordinates.emplace( delta.Coordinate.X, delta.Coordinate.Z ).second );
        REQUIRE( field.SetSample( delta.Coordinate, delta.Before, error ) );
    }
    Equal( field, before );
    for( const auto& delta : deltas ) REQUIRE( field.SetSample( delta.Coordinate, delta.After, error ) );
    Equal( field, after );
    float corner; REQUIRE( field.GetSample( { 0, 0 }, corner ) == QueryStatus::Ready ); CHECK( corner > 0 );
    for( const auto& [coord, tile] : field.GetTiles() ) CHECK( tile.Heights != before.FindTile( coord )->Heights );
}

TEST_CASE( "Terrain: brush spacing is independent of input event subdivision and stationary frames" )
{
    auto a = Flat(), b = Flat(); BrushStroke sa, sb; BrushSettings brush; brush.Radius = 2; brush.Spacing = 0.5f;
    std::string error;
    REQUIRE( sa.Begin( a, brush, -4, 0, error ) ); REQUIRE( sb.Begin( b, brush, -4, 0, error ) );
    REQUIRE( sa.Move( a, 4, 0, error ) );
    for( int x = -4; x <= 4; ++x ) { REQUIRE( sb.Move( b, x, 0, error ) ); REQUIRE( sb.Move( b, x, 0, error ) ); }
    Equal( a, b );
    sa.Cancel( a ); sb.Cancel( b ); Equal( a, Flat() ); Equal( b, Flat() );
}

TEST_CASE( "Terrain: smoothing reads a stable neighborhood and flatten captures its starting height" )
{
    auto field = Flat(); std::string error; REQUIRE( field.SetSample( { 0, 0 }, 9, error ) );
    BrushSettings brush; brush.Tool = BrushTool::Smooth; brush.Radius = 3; brush.Strength = 1; brush.Falloff = 0;
    BrushStroke stroke; REQUIRE( stroke.Begin( field, brush, 0, 0, error ) ); stroke.Finish();
    float height; REQUIRE( field.GetSample( { 0, 0 }, height ) == QueryStatus::Ready ); CHECK( height == doctest::Approx( 1 ) );
    REQUIRE( field.GetSample( { -1, 0 }, height ) == QueryStatus::Ready ); CHECK( height == doctest::Approx( 1 ) );
    field = Flat(); REQUIRE( field.SetSample( { 0, 0 }, 9, error ) );
    brush.Tool = BrushTool::Flatten;
    REQUIRE( stroke.Begin( field, brush, 0, 0, error ) ); REQUIRE( stroke.Move( field, 3, 0, error ) ); stroke.Finish();
    REQUIRE( field.GetSample( { 3, 0 }, height ) == QueryStatus::Ready ); CHECK( height == 9 );
}

TEST_CASE( "Terrain: brush clamps elevations and rejects missing pages or invalid settings atomically" )
{
    auto field = Flat( 511.9f ); BrushSettings brush; brush.Radius = 2; brush.Strength = 10;
    BrushStroke stroke; std::string error;
    REQUIRE( stroke.Begin( field, brush, 0, 0, error ) ); stroke.Finish();
    float height; field.GetSample( { 0, 0 }, height ); CHECK( height == 512 );
    field.UnloadTile( { -1, 0 } ); const auto before = field;
    CHECK_FALSE( stroke.Begin( field, brush, 0, 0, error ) ); Equal( field, before );
    field = Flat(); brush.Radius = std::numeric_limits<float>::quiet_NaN();
    CHECK_FALSE( stroke.Begin( field, brush, 0, 0, error ) ); Equal( field, Flat() );
}

TEST_CASE( "Terrain: source ray picking matches canonical triangles at seams and finite outer edges" )
{
    auto field = Flat(); std::string error; REQUIRE( field.SetSample( { 0, 0 }, 4, error ) );
    for( const auto& sample : { Vector3( 0, 100, 0 ), Vector3( -8, 100, -8 ), Vector3( 8, 100, 8 ), Vector3( 0.25f, 100, 0.25f ) } )
    {
        Vector3 hit; REQUIRE( RaycastHeightfield( field, Ray( sample, Vector3( 0, -1, 0 ) ), hit ) );
        CHECK( hit.y == doctest::Approx( field.Query( sample.x, sample.z ).Height ).epsilon( 0.0001 ) );
    }
    Vector3 hit;
    CHECK( RaycastHeightfield( field, Ray( Vector3( -10, 2, 0 ), Vector3( 1, 0, 0 ) ), hit ) );
    CHECK( hit.x == doctest::Approx( -0.5f ) );
    CHECK_FALSE( RaycastHeightfield( field, Ray( Vector3( 20, 100, 20 ), Vector3( 0, -1, 0 ) ), hit ) );
    CHECK_FALSE( RaycastHeightfield( field, Ray( Vector3( 0, 100, 0 ), Vector3( 0, 1, 0 ) ), hit ) );
    CHECK_FALSE( RaycastHeightfield( field, Ray( Vector3( 0, 100, 0 ), Vector3( 0, -1, 0 ) ), hit, 10 ) );
}

TEST_CASE( "Terrain: source generations reopen with exact borders and retain the previous readable generation" )
{
    Files files; const auto path = files.Directory / "Test.terrain";
    auto field = Flat(); std::string text, error;
    REQUIRE( SaveSource( path, field, 42, nullptr, text, error ) ); const auto originalText = text;
    Manifest original; REQUIRE( DeserializeManifest( originalText, original, error ) );
    REQUIRE( field.SetSample( { 0, 0 }, 3.14159f, error ) );
    REQUIRE( SaveSource( path, field, 42, &originalText, text, error ) ); CHECK( text != originalText );
    Heightfield reopened; uint64_t guid = 0; std::string disk;
    REQUIRE( LoadSource( path, reopened, guid, disk, error ) ); CHECK( guid == 42 ); Equal( field, reopened );
    for( const auto& page : original.Pages ) CHECK( std::filesystem::exists( files.Directory / page.Source ) );
    std::ofstream( files.Directory / "Previous.terrain" ) << originalText;
    REQUIRE( LoadSource( files.Directory / "Previous.terrain", reopened, guid, disk, error ) ); Equal( reopened, Flat() );
}

TEST_CASE( "Terrain: failed publication and external conflicts preserve the saved asset and caller outputs" )
{
    Files files; const auto path = files.Directory / "Test.terrain";
    auto field = Flat(); std::string text, error;
    REQUIRE( SaveSource( path, field, 42, nullptr, text, error ) ); const auto originalText = text;
    std::string output = "unchanged";
    // A zero asset GUID fails manifest encoding after the new pages have been staged.
    CHECK_FALSE( SaveSource( path, field, 0, &text, output, error ) );
    CHECK( Text( path ) == originalText ); CHECK( output == "unchanged" );
    size_t generations = 0;
    for( const auto& entry : std::filesystem::directory_iterator( files.Directory / "Test.terrain-data" ) ) { CHECK( entry.is_directory() ); ++generations; }
    CHECK( generations == 1 );
    const std::string stale = text + " ";
    CHECK_FALSE( SaveSource( path, field, 42, &stale, output, error ) ); CHECK( Text( path ) == originalText );
    CHECK_FALSE( SaveSource( path, field, 42, nullptr, output, error ) ); CHECK( Text( path ) == originalText );
    Heightfield reopened; uint64_t guid = 0; std::string disk;
    REQUIRE( LoadSource( path, reopened, guid, disk, error ) ); Equal( reopened, field );
    const auto before = reopened;
    std::ofstream( path ) << "broken";
    CHECK_FALSE( LoadSource( path, reopened, guid, disk, error ) ); Equal( before, reopened ); CHECK( guid == 42 ); CHECK( disk == originalText );
}
