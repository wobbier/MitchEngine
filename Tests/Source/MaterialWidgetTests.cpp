#include <doctest/doctest.h>
#include <Widgets/MaterialWidgets.h>
#include <cmath>
#include <string>

using namespace MeWidgets;
using namespace MeWidgets::MaterialMath;

TEST_CASE( "Material widgets: emissive intensity in stops" )
{
    CHECK( IntensityToEV( 1.0 ) == doctest::Approx( 0.0 ) );
    CHECK( IntensityToEV( 4.0 ) == doctest::Approx( 2.0 ) );
    CHECK( IntensityToEV( 0.25 ) == doctest::Approx( -2.0 ) );
    // 0 is the Off end; a lit intensity too dim for the field sits just above it, so "off"
    // (the Off end) is still a change that stores a true 0.
    CHECK( IntensityToEV( 0.0 ) == kOffEV );
    CHECK( IntensityToEV( -1.0 ) == kOffEV );
    CHECK( IntensityToEV( std::exp2( kOffEV - 3.0 ) ) == kDimEV );
    CHECK( IntensityToEV( 0.001 ) == kDimEV );
    CHECK( kDimEV > kOffEV );
    CHECK( EVToIntensity( kDimEV ) > 0.0 );
    CHECK( EVToIntensity( kOffEV ) == 0.0 );
    CHECK( EVToIntensity( 2.0 ) == 4.0 );
    CHECK( EVToIntensity( -1.0 ) == 0.5 );
    for( double ev = kOffEV + 0.25; ev <= kMaxEV; ev += 0.5 )
    {
        CHECK( IntensityToEV( EVToIntensity( ev ) ) == doctest::Approx( ev ) );
    }

    // The field: "+2 EV" is 4x, "4x" is +2 EV, "off" and "0x" are the Off end, shown as "Off".
    const NumberSpec spec = EmissiveSpec();
    double stored = 0.0;
    REQUIRE( ParseTyped( spec, "+2 EV", stored ) );
    CHECK( EVToIntensity( Constrain( stored, spec ) ) == 4.0 );
    REQUIRE( ParseTyped( spec, "4x", stored ) );
    CHECK( stored == doctest::Approx( 2.0 ) );
    REQUIRE( ParseTyped( spec, "0.5\xC3\x97", stored ) );
    CHECK( stored == doctest::Approx( -1.0 ) );
    REQUIRE( ParseTyped( spec, " Off ", stored ) );
    CHECK( stored == kOffEV );
    REQUIRE( ParseTyped( spec, "0x", stored ) );
    CHECK( EVToIntensity( Constrain( stored, spec ) ) == 0.0 );
    // Negative multipliers aren't intensities: refused, not Off.
    CHECK_FALSE( ParseTyped( spec, "-2x", stored ) );
    CHECK_FALSE( ParseTyped( spec, "-0.5\xC3\x97", stored ) );
    REQUIRE( ParseTyped( spec, "1", stored ) );
    CHECK( stored == 1.0 );
    CHECK_FALSE( ParseTyped( spec, "5 m", stored ) );
    CHECK( FormatValue( Unit::EV, 2.0, spec.Decimals ) == "+2.0 EV" );
    // A field without the hooks still refuses other units.
    NumberSpec plain;
    plain.FieldUnit = Unit::EV;
    CHECK_FALSE( ParseTyped( plain, "4x", stored ) );
    CHECK_FALSE( ParseTyped( plain, "off", stored ) );
}

TEST_CASE( "Material widgets: the swatch grid's cells" )
{
    CHECK( kSwatchCells == 5 );
    CHECK( CellValue( 0 ) == 0.0 );
    CHECK( CellValue( 2 ) == 0.5 );
    CHECK( CellValue( 4 ) == 1.0 );
    CHECK( CellValue( 9 ) == 1.0 );
    CHECK( NearestCell( 0.37 ) == 1 );
    CHECK( NearestCell( 0.38 ) == 2 );
    CHECK( NearestCell( -1.0 ) == 0 );
    CHECK( SnapToCell( 0.7 ) == 0.75 );
    CHECK( SnapToCell( 0.1 ) == 0.0 );
    CHECK( std::string( LooksLike( 1.0, 0.05 ) ) == "Polished metal" );
    CHECK( std::string( LooksLike( 1.0, 0.6 ) ) == "Brushed metal" );
    CHECK( std::string( LooksLike( 0.0, 0.5 ) ) == "Satin" );
    CHECK( std::string( LooksLike( 0.0, 1.0 ) ) == "Chalk" );
    CHECK( std::string( LooksLike( 0.0, 0.0 ) ) == "Lacquer" );
}

TEST_CASE( "Material widgets: swatch sphere shading" )
{
    const float red[3] = { 0.9f, 0.1f, 0.1f };
    const SphereShade plastic = ShadeSphere( red, 0.0, 0.2 );
    const SphereShade metal = ShadeSphere( red, 1.0, 0.2 );
    // A dielectric's highlight is white; a metal's is its colour.
    CHECK( plastic.Spec[0] == doctest::Approx( 1.0 ) );
    CHECK( plastic.Spec[1] == doctest::Approx( 1.0 ) );
    CHECK( metal.Spec[0] > metal.Spec[1] * 5.0 );
    // The dielectric's lit side is its albedo; the metal's body is darker (it reflects a dim sky).
    CHECK( plastic.Body[0] > plastic.Body[1] * 5.0 );
    CHECK( metal.Body[0] < plastic.Body[0] );
    CHECK( metal.Body[0] > metal.Body[1] );
    // Rims are darker than bodies for dielectrics.
    CHECK( plastic.Rim[0] < plastic.Body[0] );
    // Roughness widens and dims the highlight, monotonically.
    double lastRadius = -1.0;
    double lastPeak = 2.0;
    for( double r = 0.0; r <= 1.0; r += 0.125 )
    {
        const SphereShade shade = ShadeSphere( red, 0.0, r );
        CHECK( shade.HighlightRadius > lastRadius );
        CHECK( shade.HighlightPeak < lastPeak );
        CHECK( shade.HighlightPeak > 0.0 );
        CHECK( shade.HighlightPeak <= 1.0 );
        lastRadius = shade.HighlightRadius;
        lastPeak = shade.HighlightPeak;
    }
    // Out-of-range inputs clamp.
    const SphereShade wild = ShadeSphere( red, 3.0, -1.0 );
    const SphereShade edge = ShadeSphere( red, 1.0, 0.0 );
    CHECK( wild.HighlightRadius == doctest::Approx( edge.HighlightRadius ) );
    CHECK( wild.Body[0] == doctest::Approx( edge.Body[0] ) );
}

TEST_CASE( "Material widgets: display colours and the glow swatch" )
{
    float out[3];
    // 0..1 shows as authored (sRGB encoded).
    const double grey[3] = { 0.214041, 0.214041, 0.214041 };
    ToDisplay( grey, out );
    CHECK( out[0] == doctest::Approx( 0.5 ).epsilon( 1e-3 ) );
    // Brighter keeps the hue and rolls towards white, never past 1.
    const double hot[3] = { 8.0, 0.0, 0.0 };
    ToDisplay( hot, out );
    CHECK( out[0] == doctest::Approx( 1.0 ) );
    CHECK( out[1] > 0.5f );
    CHECK( out[1] < 1.0f );
    const float orange[3] = { 1.0f, 0.5f, 0.0f };
    EmissiveDisplay( orange, 0.0, out );
    CHECK( out[0] == 0.0f );
    EmissiveDisplay( orange, 1.0, out );
    CHECK( out[0] == doctest::Approx( 1.0 ) );
    CHECK( out[1] == doctest::Approx( 0.5 ).epsilon( 1e-3 ) );
    float brighter[3];
    EmissiveDisplay( orange, 16.0, brighter );
    CHECK( brighter[1] > out[1] );
    CHECK( brighter[2] > out[2] );
}

TEST_CASE( "Material widgets: the relief preview follows normal strength" )
{
    // Bumps peak in the cell's centre and are flat between them.
    CHECK( BumpHeight( 0.5, 0.5 ) == doctest::Approx( 1.0 ) );
    CHECK( BumpHeight( 0.0, 0.0 ) == 0.0 );
    CHECK( BumpHeight( 1.5, 2.5 ) == doctest::Approx( 1.0 ) );
    // Flat at strength 0: the same light everywhere.
    const double flat = BumpLight( 0.0, 0.0, 0.0 );
    CHECK( BumpLight( 0.4, 0.3, 0.0 ) == doctest::Approx( flat ) );
    CHECK( BumpLight( 0.7, 0.6, 0.0 ) == doctest::Approx( flat ) );
    // The side facing the light (upper left) brightens, the far side darkens, more with strength.
    auto contrast = []( double InStrength ) {
        return BumpLight( 0.35, 0.35, InStrength ) - BumpLight( 0.65, 0.65, InStrength );
    };
    CHECK( BumpLight( 0.35, 0.35, 1.0 ) > flat );
    CHECK( BumpLight( 0.65, 0.65, 1.0 ) < flat );
    CHECK( contrast( 0.0 ) == doctest::Approx( 0.0 ) );
    CHECK( contrast( 0.5 ) > 0.0 );
    CHECK( contrast( 1.0 ) > contrast( 0.5 ) );
    CHECK( contrast( 2.0 ) > contrast( 1.0 ) );
    for( double s = 0.0; s <= 2.0; s += 0.5 )
    {
        for( double u = 0.0; u < 1.0; u += 0.1 )
        {
            const double light = BumpLight( u, 0.45, s );
            CHECK( light >= 0.0 );
            CHECK( light <= 1.0 );
        }
    }
}
