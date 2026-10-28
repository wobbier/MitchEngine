#include <doctest/doctest.h>
#include "Terrain/TerrainHeightmap.h"
#include "Utils/GUID.h"
#include <bit>
#include <cmath>
#include <fstream>
#include <limits>

namespace TerrainHeightmapTests
{
    using namespace TerrainData;
    Heightfield Field()
    {
        Settings settings; settings.TileQuads = 4; settings.PatchQuads = 2;
        settings.TilesX = 2; settings.MinTile = { -1, -2 };
        Heightfield field( settings ); std::string error;
        REQUIRE( field.CreateFlatTile( { -1, -2 }, 0, error ) ); REQUIRE( field.CreateFlatTile( { 0, -2 }, 0, error ) );
        const auto min = settings.MinSample(), max = settings.MaxSample();
        for( int64_t z = min.Z; z <= max.Z; ++z ) for( int64_t x = min.X; x <= max.X; ++x ) REQUIRE( field.SetSample( { x, z }, float( x * 2 + z * 3 ), error ) );
        return field;
    }
    RawHeightmap Format( const Heightfield& field )
    {
        RawHeightmap format; format.Encoding = HeightEncoding::Float32;
        const auto& s = field.GetSettings(); format.Width = s.TilesX * s.TileQuads + 1; format.Height = s.TilesZ * s.TileQuads + 1;
        return format;
    }
    void Equal( const Heightfield& a, const Heightfield& b )
    {
        for( const auto& [coord, tile] : a.GetTiles() ) { REQUIRE( b.FindTile( coord ) ); CHECK( b.FindTile( coord )->Heights == tile.Heights ); }
    }
}
using namespace TerrainHeightmapTests;

TEST_CASE( "Terrain: RAW Float32 preserves non-square signed terrain across byte orders and orientations" )
{
    const auto field = Field(); std::string error;
    for( bool bigEndian : { false, true } ) for( bool flip : { false, true } )
    {
        auto format = Format( field ); format.BigEndian = bigEndian; format.FlipZ = flip; format.Offset = 4; format.Scale = -2;
        std::vector<uint8_t> bytes;
        REQUIRE( ExportHeightmap( field, format, bytes, error ) ); CHECK( bytes.size() == 9 * 5 * 4 );
        Heightfield decoded; REQUIRE( ImportHeightmap( bytes, field.GetSettings(), format, decoded, error ) ); Equal( field, decoded );
        // Check file orientation independently of the decoder.
        const auto min = field.GetSettings().MinSample();
        float first; REQUIRE( field.GetSample( { min.X, min.Z + ( flip ? format.Height - 1 : 0 ) }, first ) == QueryStatus::Ready );
        const auto bits = std::bit_cast<uint32_t>( float( ( first - format.Offset ) / format.Scale ) );
        for( int i = 0; i < 4; ++i ) CHECK( bytes[i] == uint8_t( bits >> ( 8 * ( bigEndian ? 3 - i : i ) ) ) );
    }
}

TEST_CASE( "Terrain: RAW UInt16 normalizes elevations and bounds quantization at shared tile borders" )
{
    const auto field = Field(); auto format = Format( field ); format.Encoding = HeightEncoding::UInt16; format.Offset = -50; format.Scale = 100;
    std::vector<uint8_t> bytes; std::string error;
    REQUIRE( ExportHeightmap( field, format, bytes, error ) );
    Heightfield decoded; REQUIRE( ImportHeightmap( bytes, field.GetSettings(), format, decoded, error ) );
    const auto min = field.GetSettings().MinSample(), max = field.GetSettings().MaxSample();
    for( auto z = min.Z; z <= max.Z; ++z ) for( auto x = min.X; x <= max.X; ++x )
    {
        float expected, actual; REQUIRE( field.GetSample( { x, z }, expected ) == QueryStatus::Ready ); REQUIRE( decoded.GetSample( { x, z }, actual ) == QueryStatus::Ready );
        CHECK( std::abs( actual - expected ) <= format.Scale / 131070 + 0.00001 );
    }
    // Decoding a high endpoint must fail if the configured physical range does not contain it.
    format.Offset = 0; format.Scale = 2000;
    const auto before = decoded;
    CHECK_FALSE( ImportHeightmap( bytes, field.GetSettings(), format, decoded, error ) ); Equal( before, decoded );
}

TEST_CASE( "Terrain: RAW import rejects malformed dimensions lengths nonfinite values and encoding without mutation" )
{
    const auto field = Field(); auto format = Format( field ); std::string error; std::vector<uint8_t> bytes;
    REQUIRE( ExportHeightmap( field, format, bytes, error ) ); Heightfield result = field;
    auto bad = format; ++bad.Width;
    CHECK_FALSE( ImportHeightmap( bytes, field.GetSettings(), bad, result, error ) ); Equal( field, result );
    bytes.pop_back(); CHECK_FALSE( ImportHeightmap( bytes, field.GetSettings(), format, result, error ) ); Equal( field, result );
    REQUIRE( ExportHeightmap( field, format, bytes, error ) );
    bytes[0] = 0; bytes[1] = 0; bytes[2] = 0xc0; bytes[3] = 0x7f;
    CHECK_FALSE( ImportHeightmap( bytes, field.GetSettings(), format, result, error ) ); Equal( field, result );
    bad = format; bad.Scale = 0;
    CHECK_FALSE( ImportHeightmap( bytes, field.GetSettings(), bad, result, error ) ); Equal( field, result );
    bad = format; bad.Encoding = static_cast<HeightEncoding>( 42 );
    CHECK_FALSE( ImportHeightmap( bytes, field.GetSettings(), bad, result, error ) );
}

TEST_CASE( "Terrain: RAW export rejects missing pages and unrepresentable samples while preserving output" )
{
    auto field = Field(); auto format = Format( field ); std::string error; std::vector<uint8_t> bytes{ 1, 2, 3 };
    format.Encoding = HeightEncoding::UInt16;
    CHECK_FALSE( ExportHeightmap( field, format, bytes, error ) ); CHECK( bytes == std::vector<uint8_t>{ 1, 2, 3 } );
    format = Format( field ); format.Scale = std::numeric_limits<double>::min();
    CHECK_FALSE( ExportHeightmap( field, format, bytes, error ) ); CHECK( bytes == std::vector<uint8_t>{ 1, 2, 3 } );
    format = Format( field ); field.UnloadTile( { -1, -2 } );
    CHECK_FALSE( ExportHeightmap( field, format, bytes, error ) ); CHECK( bytes == std::vector<uint8_t>{ 1, 2, 3 } );
}

TEST_CASE( "Terrain: RAW file publication round trips and invalid exports preserve the previous file" )
{
    const auto path = std::filesystem::path( ".tmp/Tests/TerrainHeightmaps" ) / ( std::to_string( Guid::Generate() ) + ".r32" );
    const auto field = Field(); auto format = Format( field ); std::string error;
    REQUIRE( WriteHeightmap( path, field, format, error ) );
    Heightfield decoded; REQUIRE( ReadHeightmap( path, field.GetSettings(), format, decoded, error ) ); Equal( field, decoded );
    auto invalid = format; invalid.Encoding = HeightEncoding::UInt16;
    CHECK_FALSE( WriteHeightmap( path, field, invalid, error ) );
    REQUIRE( ReadHeightmap( path, field.GetSettings(), format, decoded, error ) ); Equal( field, decoded );
    std::error_code ignored; std::filesystem::remove( path, ignored );
}
