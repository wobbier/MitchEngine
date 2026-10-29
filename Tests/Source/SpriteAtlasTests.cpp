#include "PCH.h"
#include <doctest/doctest.h>
#include "Graphics/SpriteAtlas.h"
#include "Resource/CookedAssets.h"
#include <algorithm>

using namespace Moonlight;

TEST_CASE( "Sprite atlas: MaxRects packs without overlaps, inside the size, the same way every time" )
{
    std::vector<PackRect> rects;
    uint32_t seed = 3;
    for( int i = 0; i < 60; ++i )
    {
        seed = seed * 1664525u + 1013904223u;
        PackRect rect;
        rect.Width = 4 + static_cast<int>( ( seed >> 8 ) % 60 );
        rect.Height = 4 + static_cast<int>( ( seed >> 16 ) % 60 );
        rects.push_back( rect );
    }
    std::vector<PackRect> first = rects;
    int width = 0;
    int height = 0;
    REQUIRE( PackMaxRects( first, 512, width, height ) );
    CHECK( width <= 512 );
    CHECK( height <= 512 );
    CHECK( ( width & ( width - 1 ) ) == 0 );     // powers of two
    for( size_t a = 0; a < first.size(); ++a )
    {
        CHECK( first[a].X >= 0 );
        CHECK( first[a].Y >= 0 );
        CHECK( first[a].X + first[a].Width <= width );
        CHECK( first[a].Y + first[a].Height <= height );
        for( size_t b = a + 1; b < first.size(); ++b )
        {
            const bool overlap = first[a].X < first[b].X + first[b].Width && first[b].X < first[a].X + first[a].Width
                && first[a].Y < first[b].Y + first[b].Height && first[b].Y < first[a].Y + first[a].Height;
            CHECK_FALSE( overlap );
        }
    }
    // Deterministic.
    std::vector<PackRect> second = rects;
    int width2 = 0;
    int height2 = 0;
    REQUIRE( PackMaxRects( second, 512, width2, height2 ) );
    CHECK( width2 == width );
    for( size_t i = 0; i < rects.size(); ++i )
    {
        CHECK( second[i].X == first[i].X );
        CHECK( second[i].Y == first[i].Y );
    }
    // Too big for the limit: refused.
    std::vector<PackRect> huge{ { 600, 10 } };
    CHECK_FALSE( PackMaxRects( huge, 512, width, height ) );
}

TEST_CASE( "Sprite atlas: composing copies each sprite, extrudes its edges and keeps pivots and borders" )
{
    // A 4x4 red image and a 2-sprite sheet image (green left half, blue right half).
    std::vector<uint8_t> red( 4 * 4 * 4 );
    for( size_t i = 0; i < red.size(); i += 4 )
    {
        red[i] = 255;
        red[i + 3] = 255;
    }
    std::vector<uint8_t> pair( 8 * 4 * 4 );
    for( int y = 0; y < 4; ++y )
    {
        for( int x = 0; x < 8; ++x )
        {
            uint8_t* texel = pair.data() + ( y * 8 + x ) * 4;
            texel[x < 4 ? 1 : 2] = 255;
            texel[3] = 255;
        }
    }
    std::vector<AtlasInput> inputs( 3 );
    inputs[0].Name = "Red";
    inputs[0].Rgba = red.data();
    inputs[0].ImageWidth = inputs[0].ImageHeight = 4;
    inputs[0].Pivot = glm::vec2( 0.5f, 0.f );
    inputs[1].Name = "Pair/Green";
    inputs[1].Rgba = pair.data();
    inputs[1].ImageWidth = 8;
    inputs[1].ImageHeight = 4;
    inputs[1].Rect = glm::vec4( 0.f, 0.f, 4.f, 4.f );
    inputs[1].Border = glm::vec4( 1.f );
    inputs[2] = inputs[1];
    inputs[2].Name = "Pair/Blue";
    inputs[2].Rect = glm::vec4( 4.f, 0.f, 4.f, 4.f );
    SpriteAtlasDesc desc;
    desc.Padding = 2;
    desc.Extrude = 1;
    SpriteAtlasImage atlas;
    std::string error;
    REQUIRE( ComposeSpriteAtlas( inputs, desc, atlas, error ) );
    REQUIRE( atlas.Frames.size() == 3 );
    auto texel = [&atlas]( int InX, int InY ) { return atlas.Rgba.data() + ( size_t( InY ) * atlas.Width + InX ) * 4; };
    for( size_t i = 0; i < 3; ++i )
    {
        const SpriteFrame& frame = atlas.Frames[i];
        CHECK( frame.Name == inputs[i].Name );
        CHECK( frame.Rect.z == 4.f );
        CHECK( frame.Rect.w == 4.f );
        const int channel = i == 0 ? 0 : i == 1 ? 1 : 2;
        const int x = static_cast<int>( frame.Rect.x );
        const int y = static_cast<int>( frame.Rect.y );
        // Inside, and one texel out (the extrusion), it's the sprite's colour.
        CHECK( texel( x, y )[channel] == 255 );
        CHECK( texel( x + 3, y + 3 )[channel] == 255 );
        CHECK( texel( x - 1, y )[channel] == 255 );
        CHECK( texel( x + 4, y + 3 )[channel] == 255 );
        // Beyond the extrusion, padding: clear.
        CHECK( texel( x - 2, y )[3] == 0 );
    }
    CHECK( atlas.Frames[0].Pivot.y == 0.f );
    CHECK( atlas.Frames[1].Border.x == 1.f );
}

TEST_CASE( "Sprite atlas: a cooked registry maps images and sheet sprites to their atlas sprite" )
{
    json registry = { { "Version", 1 }, { "Platform", "linux" }, { "Assets", json::object() },
        { "SpriteAtlases", { { "Assets/Textures/Sprites/Crate.png", { { "Sheet", "Assets/Atlas.spriteatlas.spritesheet" }, { "Sprite", "Crate" } } },
                             { "Assets/Coins.spritesheet|Coins_0", { { "Sheet", "Assets/Atlas.spriteatlas.spritesheet" }, { "Sprite", "Coins/Coins_0" } } } } } };
    REQUIRE( CookedAssets::Get().LoadFrom( registry ) );
    const CookedAssets::AtlasSprite* crate = CookedAssets::Get().FindAtlasSprite( "Assets/Textures/Sprites/Crate.png" );
    REQUIRE( crate != nullptr );
    CHECK( crate->Sprite == "Crate" );
    CHECK( CookedAssets::Get().FindAtlasSprite( "Assets/Coins.spritesheet|Coins_0" )->Sprite == "Coins/Coins_0" );
    CHECK( CookedAssets::Get().FindAtlasSprite( "Assets/Missing.png" ) == nullptr );
    CookedAssets::Get().Reset();
    CHECK( CookedAssets::Get().FindAtlasSprite( "Assets/Textures/Sprites/Crate.png" ) == nullptr );
}
