#include <doctest/doctest.h>
#include "Foliage/FoliageData.h"
#include "Components/Graphics/TerrainFoliage.h"
#include <cmath>
#include <limits>

namespace
{
    TerrainData::Heightfield FoliageField( bool slope = true )
    {
        TerrainData::Settings s; s.TileQuads = 16; s.PatchQuads = 8; s.MinTile = { -1, -1 }; s.TilesX = s.TilesZ = 2;
        TerrainData::Heightfield field( s ); std::string error;
        for( int z = -1; z <= 0; ++z ) for( int x = -1; x <= 0; ++x ) REQUIRE( field.CreateFlatTile( { x, z }, 0, error ) );
        if( slope ) for( int z = -16; z <= 16; ++z ) for( int x = -16; x <= 16; ++x ) REQUIRE( field.SetSample( { x, z }, x * 0.2f, error ) );
        return field;
    }


    Foliage::Data FoliagePalette() { Foliage::Data d; d.Palette = { { "Tree", "Tree.obj" }, { "Rock", "Rock.obj" } }; return d; }
}

TEST_CASE( "Foliage: deterministic strokes preserve canonical placement signed tiles spacing and variation" )
{
    auto field = FoliageField(); const auto data = FoliagePalette(); Foliage::Brush brush; brush.Radius = 7; brush.Density = 0.2f; brush.AlignNormal = true;
    Foliage::Stroke a, b; std::string error; REQUIRE( a.Begin( field, data, brush, -5, -2, error ) ); REQUIRE( a.Move( field, 5, 2, error ) );
    REQUIRE( b.Begin( field, data, brush, -5, -2, error ) ); REQUIRE( b.Move( field, 5, 2, error ) );
    REQUIRE( Foliage::Encode( a.GetData() ) == Foliage::Encode( b.GetData() ) ); REQUIRE( a.Added().size() > 20 ); std::set<TerrainData::TileCoord> tiles;
    for( const auto& i : a.GetData().Instances )
    {
        const auto hit = field.Query( i.Position.x, i.Position.z ); CHECK( i.Position.y == doctest::Approx( hit.Height ) );
        CHECK( i.Normal.InternalVector == hit.Normal.InternalVector ); CHECK( i.AlignNormal ); CHECK( i.Scale >= brush.MinScale ); CHECK( i.Scale <= brush.MaxScale ); CHECK( i.Yaw >= 0 ); CHECK( i.Yaw < 360 );
        tiles.insert( i.Tile );
        for( const auto& other : a.GetData().Instances ) if( i.ID != other.ID ) CHECK( std::hypot( i.Position.x - other.Position.x, i.Position.z - other.Position.z ) >= brush.MinSpacing - 1e-5f );
    }
    CHECK( tiles.size() == 4 ); const auto count = a.Added().size(); REQUIRE( a.Move( field, 5, 2, error ) ); CHECK( a.Added().size() == count );
}


TEST_CASE( "Foliage: pointer sampling rate does not change a stroke" )
{
    auto field = FoliageField( false ); auto data = FoliagePalette(); Foliage::Brush brush; brush.Radius = 3; brush.Density = 0.3f;
    Foliage::Stroke a, b; std::string error; REQUIRE( a.Begin( field, data, brush, -8, 0, error ) ); REQUIRE( b.Begin( field, data, brush, -8, 0, error ) );
    REQUIRE( a.Move( field, 8, 0, error ) ); for( int n = 1; n <= 128; ++n ) REQUIRE( b.Move( field, -8 + n * 0.125, 0, error ) );
    CHECK( Foliage::Encode( a.GetData() ) == Foliage::Encode( b.GetData() ) );
}


TEST_CASE( "Foliage: erase selection all-species erase and slope filtering preserve identities" )
{
    auto field = FoliageField(); auto data = FoliagePalette(); Foliage::Brush brush; brush.Radius = 8; brush.Density = 0.3f; std::string error;
    Foliage::Stroke tree; REQUIRE( tree.Begin( field, data, brush, 0, 0, error ) ); brush.SpeciesIndex = 1;
    Foliage::Stroke rock; REQUIRE( rock.Begin( field, tree.GetData(), brush, 8, 0, error ) ); data = rock.GetData();
    brush.Erase = true; brush.SpeciesIndex = 0; Foliage::Stroke erase; REQUIRE( erase.Begin( field, data, brush, 0, 0, error ) );
    REQUIRE_FALSE( erase.Removed().empty() ); for( const auto& i : erase.Removed() ) CHECK( i.SpeciesIndex == 0 );
    CHECK( erase.GetData().NextID == data.NextID ); brush.EraseAll = true; Foliage::Stroke all; REQUIRE( all.Begin( field, data, brush, 0, 0, error ) ); CHECK( all.Removed().size() >= erase.Removed().size() );
    brush = {}; brush.MaxSlope = 1; Foliage::Stroke steep; REQUIRE( steep.Begin( field, FoliagePalette(), brush, 0, 0, error ) ); CHECK( steep.GetData().Instances.empty() );
}


TEST_CASE( "Foliage: component round trips and malformed data preserve the previous state" )
{
    auto field = FoliageField(); Foliage::Stroke stroke; std::string error; REQUIRE( stroke.Begin( field, FoliagePalette(), {}, 0, 0, error ) );
    TerrainFoliage component; component.DrawDistance = 88; component.CastShadows = false; REQUIRE( component.SetData( stroke.GetData() ) ); json value; component.Serialize( value );
    TerrainFoliage restored; restored.Deserialize( value ); CHECK( restored.DrawDistance == 88 ); CHECK_FALSE( restored.CastShadows ); restored.SetEnabled( false ); restored.Deserialize( value ); CHECK( restored.IsEnabled() ); CHECK( Foliage::Encode( restored.GetData() ) == Foliage::Encode( component.GetData() ) );
    const auto original = Foliage::Encode( restored.GetData() ); REQUIRE_FALSE( original["Instances"].empty() );
    for( int kind = 0; kind < 11; ++kind )
    {
        auto broken = original;
        if( kind == 0 ) broken["Version"] = 99;
        if( kind == 1 ) broken["Instances"][0]["Scale"] = 0;
        if( kind == 2 ) broken["Instances"].push_back( broken["Instances"][0] );
        if( kind == 3 ) broken["Instances"][0]["Species"] = 50;
        if( kind == 4 ) broken["Instances"][0]["Normal"] = { 0, 0, 0 };
        if( kind == 5 ) broken["NextID"] = -1;
        if( kind == 6 ) broken["Instances"][0]["ID"] = 1.5;
        if( kind == 7 ) broken["Instances"][0]["Species"] = uint64_t( 1 ) << 32;
        if( kind == 8 ) broken["Instances"][0]["Tile"][0] = int64_t( INT32_MAX ) + 1;
        if( kind == 9 ) broken["Instances"][0]["Tile"][1] = 0.5;
        if( kind == 10 ) broken["Version"] = uint64_t( 1 ) + ( uint64_t( 1 ) << 32 );
        Foliage::Data out = restored.GetData(); CHECK_FALSE( Foliage::Decode( broken, out, error ) ); CHECK( Foliage::Encode( out ) == original );
    }
}


TEST_CASE( "Foliage: invalid brushes fail without starting and the instance cap remains usable" )
{
    auto field = FoliageField( false ); auto data = FoliagePalette(); Foliage::Stroke stroke; std::string error; Foliage::Brush brush;
    brush.Density = std::numeric_limits<float>::quiet_NaN(); CHECK_FALSE( stroke.Begin( field, data, brush, 0, 0, error ) ); CHECK_FALSE( stroke.Active() );
    brush = {}; brush.MinScale = 2; brush.MaxScale = 1; CHECK_FALSE( stroke.Begin( field, data, brush, 0, 0, error ) );
    for( size_t n = 0; n < Foliage::MaxInstances; ++n ) { Foliage::Instance i; i.ID = data.NextID++; data.Instances.push_back( i ); }
    REQUIRE( stroke.Begin( field, data, {}, 4, 4, error ) ); CHECK( stroke.GetData().Instances.size() == Foliage::MaxInstances ); CHECK( stroke.Added().empty() );
}
