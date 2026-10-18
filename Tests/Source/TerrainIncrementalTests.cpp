#include <doctest/doctest.h>
#include "Terrain/ResidentTerrain.h"

namespace
{
    TerrainData::Heightfield Field()
    {
        TerrainData::Settings s; s.TileQuads = 16; s.PatchQuads = 4; s.MinTile = { -1, -1 }; s.TilesX = s.TilesZ = 2;
        TerrainData::Heightfield field( s ); std::string error;
        for( int x = -1; x <= 0; ++x ) for( int z = -1; z <= 0; ++z ) REQUIRE( field.CreateFlatTile( { x, z }, 0, error ) );
        return field;
    }
    void EqualVector( const Vector3& a, const Vector3& b )
    {
        CHECK( a.x == b.x ); CHECK( a.y == b.y ); CHECK( a.z == b.z );
    }
    void Equal( const TerrainData::ResidentTerrain& actual, const TerrainData::ResidentTerrain& expected )
    {
        REQUIRE( actual.Patches.size() == expected.Patches.size() );
        EqualVector( actual.Bounds.Min, expected.Bounds.Min ); EqualVector( actual.Bounds.Max, expected.Bounds.Max );
        for( size_t i = 0; i < actual.Patches.size(); ++i )
        {
            const auto& a = actual.Patches[i]->Mesh; const auto& b = expected.Patches[i]->Mesh;
            REQUIRE( a.Vertices.size() == b.Vertices.size() );
            for( size_t j = 0; j < a.Vertices.size(); ++j )
            {
                EqualVector( a.Vertices[j].Position, b.Vertices[j].Position );
                EqualVector( a.Vertices[j].Normal, b.Vertices[j].Normal );
            }
            REQUIRE( a.Lods.size() == b.Lods.size() );
            for( size_t j = 0; j < a.Lods.size(); ++j )
            { CHECK( a.Lods[j].MaxError == b.Lods[j].MaxError ); CHECK( a.Lods[j].Indices == b.Lods[j].Indices ); }
        }
        REQUIRE( actual.CollisionTiles.size() == expected.CollisionTiles.size() );
        for( size_t i = 0; i < actual.CollisionTiles.size(); ++i )
        {
            REQUIRE( actual.CollisionTiles[i]->Positions.size() == expected.CollisionTiles[i]->Positions.size() );
            for( size_t j = 0; j < actual.CollisionTiles[i]->Positions.size(); ++j )
                EqualVector( actual.CollisionTiles[i]->Positions[j], expected.CollisionTiles[i]->Positions[j] );
            CHECK( actual.CollisionTiles[i]->Indices == expected.CollisionTiles[i]->Indices );
        }
    }
}

TEST_CASE( "Terrain: incremental publication shares untouched geometry and preserves old snapshots" )
{
    auto field = Field(); std::string error;
    std::shared_ptr<const TerrainData::ResidentTerrain> original, updated, full;
    REQUIRE( TerrainData::BuildResidentTerrain( field, 123, original, error ) );
    REQUIRE( field.SetSample( { -10, -10 }, 5.f, error ) );
    field.ClearDirty(); // Save points must not suppress pending preview changes.
    REQUIRE( TerrainData::UpdateResidentTerrain( field, 123, original, updated, error ) );
    CHECK( updated->BuiltPatches == 1 ); CHECK( updated->BuiltCollisionTiles == 1 );
    size_t shared = 0;
    for( size_t i = 0; i < original->Patches.size(); ++i ) shared += original->Patches[i] == updated->Patches[i];
    CHECK( shared == 63 );
    CHECK( original->Heights->Query( -10, -10 ).Height == 0.f );
    REQUIRE( TerrainData::BuildResidentTerrain( field, 123, full, error ) ); Equal( *updated, *full );
    auto previous = updated;
    REQUIRE( TerrainData::UpdateResidentTerrain( field, 123, previous, updated, error ) );
    CHECK( updated->BuiltPatches == 0 ); CHECK( updated->BuiltCollisionTiles == 0 );
    CHECK( previous->Patches == updated->Patches ); CHECK( previous->CollisionTiles == updated->CollisionTiles );
}

TEST_CASE( "Terrain: border normal halos and four-way corners match a full build through undo" )
{
    auto field = Field(); std::string error;
    std::shared_ptr<const TerrainData::ResidentTerrain> previous, updated, full;
    REQUIRE( TerrainData::BuildResidentTerrain( field, 123, previous, error ) );
    for( const auto sample : { TerrainData::SampleCoord{ -1, -10 }, { 0, -10 }, { 0, 0 }, { 16, 16 } } )
    {
        for( float height : { 7.f, 0.f } )
        {
            REQUIRE( field.SetSample( sample, height, error ) );
            REQUIRE( TerrainData::UpdateResidentTerrain( field, 123, previous, updated, error ) );
            REQUIRE( TerrainData::BuildResidentTerrain( field, 123, full, error ) ); Equal( *updated, *full );
            CHECK( updated->BuiltPatches == ( sample == TerrainData::SampleCoord{ 16, 16 } ? 1 : ( sample.Z == 0 ? 4 : 2 ) ) );
            CHECK( updated->BuiltCollisionTiles == ( sample.X == -1 || sample.X == 16 ? 1 : ( sample.Z == 0 ? 4 : 2 ) ) );
            previous = updated;
        }
    }
}

TEST_CASE( "Terrain: incremental publication rebuilds incompatible data and preserves output on failure" )
{
    auto field = Field(); std::string error;
    std::shared_ptr<const TerrainData::ResidentTerrain> original, updated;
    REQUIRE( TerrainData::BuildResidentTerrain( field, 123, original, error ) );
    REQUIRE( TerrainData::UpdateResidentTerrain( field, 456, original, updated, error ) );
    CHECK( updated->BuiltPatches == 64 ); CHECK( updated->BuiltCollisionTiles == 4 );
    auto settings = field.GetSettings(); settings.SampleSpacing = 2.f;
    TerrainData::Heightfield scaled( settings );
    for( int x = -1; x <= 0; ++x ) for( int z = -1; z <= 0; ++z ) REQUIRE( scaled.CreateFlatTile( { x, z }, 0, error ) );
    REQUIRE( TerrainData::UpdateResidentTerrain( scaled, 123, original, updated, error ) );
    CHECK( updated->BuiltPatches == 64 ); CHECK( updated->BuiltCollisionTiles == 4 );
    CHECK( updated->Bounds.Max.x == 32.f );
    field.UnloadTile( { 0, 0 } ); auto retained = updated;
    CHECK_FALSE( TerrainData::UpdateResidentTerrain( field, 456, original, updated, error ) ); CHECK( retained == updated );
    CHECK_FALSE( error.empty() );
}
