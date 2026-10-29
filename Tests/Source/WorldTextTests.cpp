#include <doctest/doctest.h>
#include "Components/Graphics/TextRenderer.h"
#include "Path.h"
#include "Text/FontAtlas.h"

TEST_CASE( "World text: UTF-8 decoding" )
{
    const std::string text = "A\xC3\xA9\xE2\x82\xAC\xF0\x9F\x99\x82\xFF";   // A, e-acute, euro, an emoji, a bad byte
    size_t at = 0;
    CHECK( FontAtlas::NextCodepoint( text, at ) == 'A' );
    CHECK( FontAtlas::NextCodepoint( text, at ) == 0xE9 );
    CHECK( FontAtlas::NextCodepoint( text, at ) == 0x20AC );
    CHECK( FontAtlas::NextCodepoint( text, at ) == 0x1F642 );
    CHECK( FontAtlas::NextCodepoint( text, at ) == 0xFFFD );
    CHECK( at == text.size() );
}


TEST_CASE( "World text: SDF glyphs from the engine font, and layout measures" )
{
    const std::string font = "Engine/Assets/Fonts/Inter-Regular.ttf";
    if( !Path( font ).Exists )
    {
        return;
    }
    FontAtlas* atlas = FontAtlas::Get( font );
    REQUIRE( atlas );
    const FontAtlas::Glyph& a = atlas->GetGlyph( 'A' );
    CHECK( a.Visible );
    CHECK( a.Advance > 0.2f );
    CHECK( a.Advance < 1.f );
    CHECK( a.Uv.z > a.Uv.x );
    CHECK( a.Size.y > 0.5f );     // a capital plus the SDF spread, in line heights
    const FontAtlas::Glyph& space = atlas->GetGlyph( ' ' );
    CHECK_FALSE( space.Visible );
    CHECK( space.Advance > 0.f );
    CHECK( atlas->GetAscender() > 0.5f );
    CHECK( atlas->GetAscender() < 1.f );

    TextRenderer text;
    text.Size = 2.f;
    text.Text = "Hi";
    const glm::vec2 short_ = text.Measure();
    text.Text = "Hello there";
    const glm::vec2 long_ = text.Measure();
    CHECK( long_.x > short_.x );
    CHECK( short_.y == doctest::Approx( 2.f ) );
    text.Text = "Two\nlines";
    text.LineSpacing = 1.5f;
    CHECK( text.Measure().y == doctest::Approx( 2.f * 2.5f ) );
}
