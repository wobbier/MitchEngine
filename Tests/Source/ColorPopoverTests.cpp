#include <doctest/doctest.h>
#include <Widgets/ColorPopover.h>
#include <Widgets/Color.h>
#include <cmath>
#include <string>

using namespace MeWidgets;
using namespace MeWidgets::ColorMath;

namespace
{
    bool Same( const Rgb& InA, const Rgb& InB, float InTolerance = 1e-4f )
    {
        for( int i = 0; i < 3; ++i )
        {
            if( std::abs( InA[i] - InB[i] ) > InTolerance ) return false;
        }
        return true;
    }

    Rgb Bytes( int InR, int InG, int InB )
    {
        return { InR / 255.f, InG / 255.f, InB / 255.f };
    }
}

TEST_CASE( "Colour popover: hex and rgb() entry" )
{
    Rgb rgb {};
    float alpha = 0.25f;
    CHECK( ParseColor( "#FF8800", rgb, &alpha ) );
    CHECK( Same( rgb, Bytes( 255, 136, 0 ) ) );
    CHECK( alpha == 0.25f );    // no alpha given: untouched
    CHECK( ParseColor( "ff8800", rgb ) );
    CHECK( Same( rgb, Bytes( 255, 136, 0 ) ) );
    CHECK( ParseColor( "  #f80  ", rgb ) );
    CHECK( Same( rgb, Bytes( 255, 136, 0 ) ) );
    CHECK( ParseColor( "0x336699", rgb ) );
    CHECK( Same( rgb, Bytes( 0x33, 0x66, 0x99 ) ) );
    CHECK( ParseColor( "#FF880080", rgb, &alpha ) );
    CHECK( alpha == doctest::Approx( 128.f / 255.f ) );
    CHECK( ParseColor( "#F808", rgb, &alpha ) );
    CHECK( alpha == doctest::Approx( 0x88 / 255.f ) );

    CHECK( ParseColor( "rgb(255, 136, 0)", rgb ) );
    CHECK( Same( rgb, Bytes( 255, 136, 0 ) ) );
    CHECK( ParseColor( "RGB(255 136 0)", rgb ) );
    CHECK( Same( rgb, Bytes( 255, 136, 0 ) ) );
    CHECK( ParseColor( "rgb(100%, 50%, 0%)", rgb ) );
    CHECK( Same( rgb, { 1.f, 0.5f, 0.f } ) );
    CHECK( ParseColor( "rgba(255, 0, 0, 0.5)", rgb, &alpha ) );
    CHECK( alpha == doctest::Approx( 0.5f ) );
    // Decimals that all stay within 1 are the engine's own 0..1 channels; integers are CSS bytes.
    CHECK( ParseColor( "rgb(1.0, 0.5, 0.0)", rgb ) );
    CHECK( Same( rgb, { 1.f, 0.5f, 0.f } ) );
    CHECK( ParseColor( "rgb(1, 0, 0)", rgb ) );
    CHECK( Same( rgb, Bytes( 1, 0, 0 ) ) );

    std::string error;
    CHECK_FALSE( ParseColor( "", rgb, nullptr, &error ) );
    CHECK_FALSE( ParseColor( "#GG0000", rgb, nullptr, &error ) );
    CHECK( error.find( "hex digit" ) != std::string::npos );
    CHECK( ParseColor( "#FF88", rgb ) );     // 4 digits: #RGBA
    CHECK_FALSE( ParseColor( "#FF880", rgb, nullptr, &error ) );
    CHECK_FALSE( ParseColor( "rgb(300, 0, 0)", rgb, nullptr, &error ) );
    CHECK_FALSE( ParseColor( "rgb(1, 2)", rgb, nullptr, &error ) );
    CHECK_FALSE( ParseColor( "rgb(1, 2, 3", rgb, nullptr, &error ) );
    CHECK_FALSE( ParseColor( "orange", rgb, nullptr, &error ) );
}

TEST_CASE( "Colour popover: hex formatting round-trips" )
{
    CHECK( FormatHex( { 1.f, 136.f / 255.f, 0.f } ) == "#FF8800" );
    CHECK( FormatHex( { 2.f, -1.f, 0.5f } ) == "#FF0080" );     // clamped, rounded
    CHECK( FormatHex( { 1.f, 1.f, 1.f }, 1.f ) == "#FFFFFF" );
    CHECK( FormatHex( { 1.f, 1.f, 1.f }, 0.5f ) == "#FFFFFF80" );
    for( int value = 0; value < 256; value += 17 )
    {
        const Rgb color = Bytes( value, 255 - value, value / 2 );
        Rgb back {};
        REQUIRE( ParseColor( FormatHex( color ), back ) );
        CHECK( Same( back, color ) );
    }
}

TEST_CASE( "Colour popover: HDR intensity in stops" )
{
    CHECK( EvToMultiplier( 2.0 ) == doctest::Approx( 4.0 ) );
    CHECK( MultiplierToEv( 4.0 ) == doctest::Approx( 2.0 ) );
    CHECK( MultiplierToEv( 0.5 ) == doctest::Approx( -1.0 ) );
    CHECK( std::isnan( MultiplierToEv( 0.0 ) ) );

    // Above 1: a base at full brightness and the stops above it.
    HdrSplit split = SplitHdr( { 4.f, 2.f, 0.f } );
    CHECK( split.Ev == doctest::Approx( 2.0 ) );
    CHECK( Same( split.Base, { 1.f, 0.5f, 0.f } ) );
    // Inside 0..1: itself, at 0 EV.
    split = SplitHdr( { 0.5f, 0.25f, 0.f } );
    CHECK( split.Ev == 0.0 );
    CHECK( Same( split.Base, { 0.5f, 0.25f, 0.f } ) );

    CHECK( Same( WithIntensity( { 0.5f, 0.25f, 0.f }, 2.0 ), { 4.f, 2.f, 0.f } ) );
    CHECK( Same( WithIntensity( { 4.f, 2.f, 0.f }, 1.0 ), { 2.f, 1.f, 0.f } ) );
    CHECK( Same( WithIntensity( { 4.f, 2.f, 0.f }, 0.0 ), { 1.f, 0.5f, 0.f } ) );
    CHECK( Same( WithIntensity( { 0.5f, 0.25f, 0.f }, 0.0 ), { 0.5f, 0.25f, 0.f } ) );
    CHECK( Same( WithIntensity( { 0.f, 0.f, 0.f }, 3.0 ), { 0.f, 0.f, 0.f } ) );
    CHECK( SplitHdr( WithIntensity( { 0.2f, 0.9f, 0.4f }, 3.5 ) ).Ev == doctest::Approx( 3.5 ) );
    CHECK( Same( Scaled( { 1.f, 0.5f, 0.f }, 1.0 ), { 2.f, 1.f, 0.f } ) );

    CHECK( FormatIntensity( 2.0 ) == "+2 EV" );
    CHECK( FormatIntensity( 1.5 ) == "+1.5 EV" );
    CHECK( FormatIntensity( 0.0 ) == "0 EV" );
    CHECK( FormatIntensity( 0.04 ) == "0 EV" );
    CHECK( FormatIntensity( 2.26 ) == "+2.3 EV" );
    CHECK( FormatMultiplier( 2.0 ) == "\xC3\x97" "4" );
    CHECK( FormatMultiplier( 1.5 ) == "\xC3\x97" "2.83" );

    double ev = -1.0;
    CHECK( ParseIntensity( "+2 EV", ev ) );
    CHECK( ev == doctest::Approx( 2.0 ) );
    CHECK( ParseIntensity( "3", ev ) );
    CHECK( ev == doctest::Approx( 3.0 ) );
    CHECK( ParseIntensity( "4x", ev ) );
    CHECK( ev == doctest::Approx( 2.0 ) );
    CHECK( ParseIntensity( "\xC3\x97" "8", ev ) );
    CHECK( ev == doctest::Approx( 3.0 ) );
    CHECK( ParseIntensity( "-1 EV", ev ) );
    CHECK( ev == 0.0 );     // nothing below white: dim the colour itself
    CHECK_FALSE( ParseIntensity( "bright", ev ) );
}

TEST_CASE( "Colour popover: what the swatch shows and pastes" )
{
    const float ldr[3] = { 1.f, 136.f / 255.f, 0.f };
    CHECK( Describe( ldr, 3, false ) == "#FF8800" );
    const float hdr[3] = { 4.f, 136.f / 255.f * 4.f, 0.f };
    CHECK( Describe( hdr, 3, true ) == "#FF8800 +2 EV" );
    CHECK( Describe( ldr, 3, true ) == "#FF8800 0 EV" );
    const float rgba[4] = { 1.f, 136.f / 255.f, 0.f, 0.5f };
    CHECK( Describe( rgba, 4, false ) == "#FF880080" );
    const float opaque[4] = { 1.f, 136.f / 255.f, 0.f, 1.f };
    CHECK( Describe( opaque, 4, false ) == "#FF8800" );
    for( const std::string& shown : { Describe( ldr, 3, false ), Describe( hdr, 3, true ), Describe( rgba, 4, false ) } )
    {
        CHECK( shown.find( '~' ) == std::string::npos );
    }

    Rgb rgb {};
    float alpha = 1.f;
    double ev = 0.0;
    CHECK( ParseSwatchText( "#FF8800 +2 EV", rgb, &alpha, &ev ) );
    CHECK( Same( rgb, Bytes( 255, 136, 0 ) ) );
    CHECK( ev == doctest::Approx( 2.0 ) );
    ev = 1.0;
    CHECK( ParseSwatchText( "#00FF00", rgb, &alpha, &ev ) );
    CHECK( ev == 1.0 );     // no intensity given: the field keeps its own
    CHECK( ParseSwatchText( "rgb(255, 0, 0) 4x", rgb, &alpha, &ev ) );
    CHECK( ev == doctest::Approx( 2.0 ) );
    CHECK( ParseSwatchText( "#FF000080", rgb, &alpha, &ev ) );
    CHECK( alpha == doctest::Approx( 128.f / 255.f ) );
    CHECK_FALSE( ParseSwatchText( "[1, 0, 0]", rgb, &alpha, &ev ) );
    CHECK_FALSE( ParseSwatchText( "#FF0000 loud", rgb, &alpha, &ev ) );
}

TEST_CASE( "Colour popover: the scene palette" )
{
    // Repeats merge, near-identical colours merge, most used first, first seen breaks ties.
    const std::vector<Rgb> colors = {
        { 1.f, 0.f, 0.f },
        { 0.f, 0.f, 1.f },
        { 0.f, 0.f, 1.f },
        { 0.f, 1.f, 0.f },
        { 0.f, 0.f, 0.998f },   // the blue again, as far as anyone can see
        { 1.f, 0.f, 0.f },
        { 0.5f, 0.5f, 0.5f },
        { 2.f, 0.f, 0.f },      // clamped: the red
        { NAN, 0.f, 0.f },      // skipped
    };
    const std::vector<PaletteEntry> palette = BuildPalette( colors );
    REQUIRE( palette.size() == 4 );
    CHECK( Same( palette[0].Color, { 1.f, 0.f, 0.f } ) );
    CHECK( palette[0].Uses == 3 );
    CHECK( Same( palette[1].Color, { 0.f, 0.f, 1.f } ) );
    CHECK( palette[1].Uses == 3 );
    CHECK( Same( palette[2].Color, { 0.f, 1.f, 0.f } ) );
    CHECK( Same( palette[3].Color, { 0.5f, 0.5f, 0.5f } ) );

    // Greys a visible step apart stay apart; at most InMax.
    std::vector<Rgb> greys;
    for( int i = 0; i < 20; ++i )
    {
        const float v = i / 19.f;
        greys.push_back( { v, v, v } );
    }
    CHECK( BuildPalette( greys ).size() == 12 );
    CHECK( BuildPalette( greys, 30 ).size() == 20 );
    CHECK( BuildPalette( {} ).empty() );

    // Perceptual distance: black to white is about 1, a blue step is small.
    CHECK( PerceptualDistance( { 0.f, 0.f, 0.f }, { 1.f, 1.f, 1.f } ) == doctest::Approx( 1.0 ).epsilon( 0.01 ) );
    CHECK( PerceptualDistance( { 0.f, 0.f, 1.f }, { 0.f, 0.f, 0.998f } ) < 0.01f );
}

TEST_CASE( "Colour popover: temperature estimate" )
{
    double kelvin = 0.0;
    for( double k : { 1200.0, 1900.0, 3200.0, 5600.0, 9000.0, 15000.0, 19500.0 } )
    {
        CAPTURE( k );
        REQUIRE( EstimateKelvin( ColorTemperature::ToSrgb( k ), kelvin ) );
        // A step on the curve is well under a byte above ~6000 K, so compare colours, not kelvin.
        CHECK( Same( ColorTemperature::ToSrgb( kelvin ), ColorTemperature::ToSrgb( k ), 0.01f ) );
    }
    // Darker versions of the same light are still that temperature.
    const Rgb candle = ColorTemperature::ToSrgb( 1900.0 );
    REQUIRE( EstimateKelvin( { candle[0] * 0.5f, candle[1] * 0.5f, candle[2] * 0.5f }, kelvin ) );
    CHECK( kelvin == doctest::Approx( 1900.0 ).epsilon( 0.02 ) );
    // Off the curve: green, magenta, black.
    CHECK_FALSE( EstimateKelvin( { 0.f, 1.f, 0.f }, kelvin ) );
    CHECK_FALSE( EstimateKelvin( { 1.f, 0.f, 1.f }, kelvin ) );
    CHECK_FALSE( EstimateKelvin( { 0.f, 0.f, 0.f }, kelvin ) );
    // The ranged search reaches past the Light inspector's 12000 K.
    REQUIRE( ColorTemperature::FromSrgbInRange( ColorTemperature::ToSrgb( 18000.0 ), kKelvinMin, kKelvinMax, kelvin ) );
    CHECK( Same( ColorTemperature::ToSrgb( kelvin ), ColorTemperature::ToSrgb( 18000.0 ), 0.01f ) );
}
