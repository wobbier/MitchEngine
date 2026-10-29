#include <doctest/doctest.h>
#include "Graphics/SpriteSheet.h"
#include "Path.h"
#include <vector>

namespace
{
    // An RGBA8 image with opaque rectangles (x, y, w, h) on transparent texels.
    std::vector<uint8_t> MakeSheet( int InWidth, int InHeight, std::initializer_list<glm::ivec4> InRects )
    {
        std::vector<uint8_t> rgba( static_cast<size_t>( InWidth ) * InHeight * 4, 0 );
        for( const glm::ivec4& rect : InRects )
        {
            for( int y = rect.y; y < rect.y + rect.w; ++y )
            {
                for( int x = rect.x; x < rect.x + rect.z; ++x )
                {
                    uint8_t* texel = &rgba[( static_cast<size_t>( y ) * InWidth + x ) * 4];
                    texel[0] = texel[1] = texel[2] = texel[3] = 255;
                }
            }
        }
        return rgba;
    }
}

TEST_CASE( "Sprite sheets: grid slicing (offset, padding, empty cells skipped)" )
{
    // A 3 x 2 grid of 8 x 8 cells with 2 px padding from (1, 1); the middle-bottom cell is empty.
    const std::vector<uint8_t> image = MakeSheet( 32, 22, { { 2, 2, 4, 4 }, { 12, 1, 8, 8 }, { 21, 3, 2, 2 }, { 1, 11, 1, 1 }, { 25, 15, 3, 3 } } );
    Moonlight::SpriteSlicing slicing;
    slicing.CellWidth = 8;
    slicing.CellHeight = 8;
    slicing.OffsetX = 1;
    slicing.OffsetY = 1;
    slicing.PaddingX = 2;
    slicing.PaddingY = 2;
    slicing.Pivot = glm::vec2( 0.5f, 0.f );
    const std::vector<Moonlight::SpriteFrame> frames = Moonlight::SpriteSheet::SliceGrid( image.data(), 32, 22, slicing, "Hero" );
    REQUIRE( frames.size() == 5 );
    CHECK( frames[0].Name == "Hero_0" );
    CHECK( frames[0].Rect == glm::vec4( 1, 1, 8, 8 ) );
    CHECK( frames[1].Rect == glm::vec4( 11, 1, 8, 8 ) );
    CHECK( frames[2].Rect == glm::vec4( 21, 1, 8, 8 ) );
    CHECK( frames[3].Rect == glm::vec4( 1, 11, 8, 8 ) );
    CHECK( frames[4].Rect == glm::vec4( 21, 11, 8, 8 ) );   // (11, 11) was empty
    CHECK( frames[4].Pivot == glm::vec2( 0.5f, 0.f ) );
    slicing.SkipEmpty = false;
    CHECK( Moonlight::SpriteSheet::SliceGrid( image.data(), 32, 22, slicing, "Hero" ).size() == 6 );
}


TEST_CASE( "Sprite sheets: auto slicing finds opaque islands in reading order" )
{
    // Two on the top row (the right one taller), one below; diagonal texels join an island.
    std::vector<uint8_t> image = MakeSheet( 40, 30, { { 20, 2, 6, 10 }, { 2, 4, 5, 5 }, { 10, 20, 8, 4 } } );
    image[( static_cast<size_t>( 9 ) * 40 + 7 ) * 4 + 3] = 255;   // (7, 9): diagonal to (6, 8)
    Moonlight::SpriteSlicing slicing;
    slicing.Mode = Moonlight::SpriteSliceMode::Auto;
    const std::vector<Moonlight::SpriteFrame> frames = Moonlight::SpriteSheet::SliceAuto( image.data(), 40, 30, slicing, "Gem" );
    REQUIRE( frames.size() == 3 );
    CHECK( frames[0].Rect == glm::vec4( 2, 4, 6, 6 ) );     // the island grew by the diagonal texel
    CHECK( frames[1].Rect == glm::vec4( 20, 2, 6, 10 ) );
    CHECK( frames[2].Rect == glm::vec4( 10, 20, 8, 4 ) );
    CHECK( frames[2].Name == "Gem_2" );
}


TEST_CASE( "Sprite sheets: JSON round trip, lookup, and slicing a real sheet" )
{
    Moonlight::SpriteSheet sheet{ Path( "" ) };
    sheet.Texture = "Assets/Textures/Sprites/Coins.png";
    sheet.Slicing.CellWidth = 16;
    sheet.Slicing.CellHeight = 16;
    sheet.Sprites.push_back( { "Coin", glm::vec4( 0, 0, 16, 16 ), glm::vec2( 0.5f, 0.f ), glm::vec4( 1, 2, 3, 4 ) } );
    Moonlight::SpriteSheet copy{ Path( "" ) };
    REQUIRE( copy.Parse( sheet.Write() ) );
    CHECK( copy.Texture == sheet.Texture );
    CHECK( copy.Slicing.CellWidth == 16 );
    REQUIRE( copy.Find( "Coin" ) );
    CHECK( copy.Find( "Coin" )->Border == glm::vec4( 1, 2, 3, 4 ) );
    CHECK( copy.Find( "Missing" ) == nullptr );
    CHECK_FALSE( copy.Parse( "[1, 2]" ) );

    // The game's coin strip: four 16 x 16 frames (a hand-set pivot survives the re-slice by name).
    if( Path( sheet.Texture ).Exists )
    {
        sheet.Sprites = { { "Coins_2", glm::vec4( 0.f ), glm::vec2( 0.25f, 0.75f ), glm::vec4( 0.f ) } };
        REQUIRE( sheet.Slice() );
        REQUIRE( sheet.Sprites.size() == 4 );
        CHECK( sheet.Sprites[3].Name == "Coins_3" );
        CHECK( sheet.Sprites[3].Rect == glm::vec4( 48, 0, 16, 16 ) );
        CHECK( sheet.Sprites[2].Pivot == glm::vec2( 0.25f, 0.75f ) );
        // Auto finds each coin's own extent: the narrowest frame is a sliver.
        sheet.Slicing.Mode = Moonlight::SpriteSliceMode::Auto;
        REQUIRE( sheet.Slice() );
        REQUIRE( sheet.Sprites.size() == 4 );
        CHECK( sheet.Sprites[3].Rect.z < sheet.Sprites[0].Rect.z );
    }
}
