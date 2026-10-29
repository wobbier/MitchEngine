#include <doctest/doctest.h>
#include "Components/Graphics/Tilemap.h"
#include "JSON.h"
#include <algorithm>

TEST_CASE( "Tilemaps: cells, fills and erasing against the palette" )
{
    Tilemap tilemap;
    tilemap.Palette = { "Grass", "Brick" };
    CHECK( tilemap.GetTile( 0, 0 ) == -1 );
    const uint32_t revision = tilemap.GetRevision();
    tilemap.SetTile( 2, -1, 1 );
    CHECK( tilemap.GetTile( 2, -1 ) == 1 );
    CHECK( tilemap.GetRevision() != revision );
    tilemap.FillRect( 0, 0, 3, 1, 0 );
    CHECK( tilemap.GetTileCount() == 9 );
    tilemap.SetTile( 0, 0, 5 );     // outside the palette: erases
    CHECK( tilemap.GetTile( 0, 0 ) == -1 );
    tilemap.SetTile( 9, 9, -1 );    // erasing an empty cell changes nothing
    CHECK( tilemap.GetTileCount() == 8 );
    int x = 0;
    int y = 0;
    tilemap.CellSize = 0.5f;
    tilemap.LocalToCell( Vector2( 1.2f, -0.1f ), x, y );
    CHECK( x == 2 );
    CHECK( y == -1 );
    tilemap.Clear();
    CHECK( tilemap.GetTileCount() == 0 );
}


TEST_CASE( "Tilemaps: autotiling picks Base_<mask> from the four neighbours" )
{
    Tilemap tilemap;
    tilemap.Palette = { "@Ground", "Brick" };
    tilemap.FillRect( 0, 0, 2, 0, 0 );   // a row of three
    tilemap.SetTile( 1, 1, 0 );          // one on top of the middle
    tilemap.SetTile( 3, 0, 1 );          // a brick next to the row: a different entry
    // No sheet loaded: every variant name is used as is.
    CHECK( tilemap.GetSpriteName( 0, 0 ) == "Ground_2" );     // right
    CHECK( tilemap.GetSpriteName( 1, 0 ) == "Ground_11" );    // up + right + left
    CHECK( tilemap.GetSpriteName( 2, 0 ) == "Ground_8" );     // left (the brick isn't Ground)
    CHECK( tilemap.GetSpriteName( 1, 1 ) == "Ground_4" );     // down
    CHECK( tilemap.GetSpriteName( 3, 0 ) == "Brick" );
    CHECK( tilemap.GetSpriteName( 5, 5 ).empty() );
}


TEST_CASE( "Tilemaps: the collider outline merges runs of boundary edges" )
{
    Tilemap tilemap;
    tilemap.Palette = { "Stone" };
    tilemap.CellSize = 2.f;
    tilemap.FillRect( 0, 0, 3, 0, 0 );   // a 4 x 1 bar
    std::vector<std::pair<Vector2, Vector2>> outline = tilemap.BuildOutline();
    // Top, bottom, left and right: four segments, not ten unit edges.
    REQUIRE( outline.size() == 4 );
    float length = 0.f;
    for( const auto& [from, to] : outline )
    {
        length += ( to - from ).Length();
    }
    CHECK( length == doctest::Approx( 2.f * ( 8.f + 2.f ) ) );
    // An L: the inner corner adds edges; a hole in a 3 x 3 block adds an inner ring.
    tilemap.Clear();
    tilemap.FillRect( 0, 0, 2, 2, 0 );
    tilemap.SetTile( 1, 1, -1 );
    outline = tilemap.BuildOutline();
    CHECK( outline.size() == 8 );
}


TEST_CASE( "Tilemaps: tiles round-trip through the scene JSON" )
{
    Tilemap tilemap;
    tilemap.Palette = { "Grass", "Brick" };
    tilemap.CellSize = 0.75f;
    tilemap.SetTile( -3, 4, 1 );
    tilemap.SetTile( 7, -2, 0 );
    json saved;
    tilemap.Serialize( saved );
    CHECK( saved["Tiles"].size() == 2 );
    Tilemap loaded;
    loaded.Deserialize( saved );
    CHECK( loaded.GetTile( -3, 4 ) == 1 );
    CHECK( loaded.GetTile( 7, -2 ) == 0 );
    CHECK( loaded.CellSize == doctest::Approx( 0.75f ) );
    CHECK( loaded.Palette.size() == 2 );
}
