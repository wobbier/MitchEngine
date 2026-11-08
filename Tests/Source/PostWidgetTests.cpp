#include <doctest/doctest.h>
#include <Widgets/PostWidgets.h>
#include <cmath>
#include <string>

using namespace MeWidgets;

// Reference values were computed independently from Assets/Shaders/Post/PostCommon.sh and
// Tonemap.frag (the same matrices and polynomials, evaluated in Python).
TEST_CASE( "Post widgets: tonemap curves match the shader" )
{
    using namespace PostMath;
    // None clamps; Reinhard (luma) is x / (1 + x) on a grey.
    CHECK( TonemapGrey( 0, 0.5 ) == doctest::Approx( 0.5 ) );
    CHECK( TonemapGrey( 0, 3.0 ) == doctest::Approx( 1.0 ) );
    CHECK( TonemapGrey( 3, 0.18 ) == doctest::Approx( 0.18 / 1.18 ) );
    // ACES fitted (Hill).
    CHECK( TonemapGrey( 1, 0.18 ) == doctest::Approx( 0.105591 ).epsilon( 1e-4 ) );
    CHECK( TonemapGrey( 1, 1.0 ) == doctest::Approx( 0.619115 ).epsilon( 1e-4 ) );
    CHECK( TonemapGrey( 1, 4.0 ) == doctest::Approx( 0.909013 ).epsilon( 1e-4 ) );
    // AgX (Wrensch's minimal fit), returned linear.
    CHECK( TonemapGrey( 2, 0.18 ) == doctest::Approx( 0.214519 ).epsilon( 1e-4 ) );
    CHECK( TonemapGrey( 2, 1.0 ) == doctest::Approx( 0.590159 ).epsilon( 1e-4 ) );
    CHECK( TonemapGrey( 2, 0.01 ) == doctest::Approx( 0.005995 ).epsilon( 1e-3 ) );

    // Every curve rises and stays in 0..1.
    for( int op = 0; op < kTonemapperCount; ++op )
    {
        double last = -1.0;
        for( double x = 0.001; x < 100.0; x *= 1.3 )
        {
            const double y = TonemapGrey( op, x );
            CHECK( y >= last - 1e-9 );
            CHECK( y >= 0.0 );
            CHECK( y <= 1.0 );
            last = y;
        }
    }
    CHECK( std::string( TonemapperName( 2 ) ) == "AgX" );
}

TEST_CASE( "Post widgets: display values include exposure and the sRGB encode" )
{
    using namespace PostMath;
    CHECK( SrgbEncode( 0.0 ) == doctest::Approx( 0.0 ) );
    CHECK( SrgbEncode( 1.0 ) == doctest::Approx( 1.0 ) );
    CHECK( SrgbEncode( 0.18 ) == doctest::Approx( 0.461356 ).epsilon( 1e-4 ) );
    for( double v : { 0.001, 0.02, 0.18, 0.5, 0.9 } )
    {
        CHECK( SrgbDecode( SrgbEncode( v ) ) == doctest::Approx( v ) );
    }
    // Mid grey on screen (the Tonemap Curve's reading): ACES 0.36 at 0 EV.
    CHECK( DisplayGrey( 1, 0.18, 0.0 ) == doctest::Approx( 0.358457 ).epsilon( 1e-4 ) );
    CHECK( DisplayGrey( 2, 0.18, 0.0 ) == doctest::Approx( 0.500516 ).epsilon( 1e-4 ) );
    CHECK( DisplayGrey( 3, 0.18, 0.0 ) == doctest::Approx( 0.426946 ).epsilon( 1e-4 ) );
    // The Fog showcase's -0.2 EV.
    CHECK( DisplayGrey( 1, 0.18, -0.2 ) == doctest::Approx( 0.324174 ).epsilon( 1e-4 ) );
    CHECK( DisplayGrey( 2, 0.18, -0.2 ) == doctest::Approx( 0.474969 ).epsilon( 1e-4 ) );
    // One stop up is the same as twice the light.
    CHECK( DisplayGrey( 1, 0.09, 1.0 ) == doctest::Approx( DisplayGrey( 1, 0.18, 0.0 ) ) );
}

TEST_CASE( "Post widgets: lift, gamma and gain solve back to the curve" )
{
    using namespace PostMath;
    Grade neutral;
    for( double x : { 0.0, 0.1, 0.5, 1.0 } )
    {
        CHECK( GradeChannel( neutral, x ) == doctest::Approx( x ) );
    }
    // Tonemap.frag: c = (c - 0.18) k + 0.18; c = c gain + lift (1 - c); c^(1 / gamma).
    Grade grade;
    grade.Contrast = 1.2;
    grade.Lift = 0.05;
    grade.Gamma = 1.3;
    grade.Gain = 0.9;
    const double c = ( 0.4 - 0.18 ) * 1.2 + 0.18;
    CHECK( GradeChannel( grade, 0.4 ) == doctest::Approx( std::pow( c * 0.9 + 0.05 * ( 1.0 - c ), 1.0 / 1.3 ) ) );
    // Negative lift crushes black: the shader clamps, the signed curve shows how far.
    grade.Lift = -0.1;
    CHECK( GradeChannel( grade, 0.0 ) == doctest::Approx( 0.0 ) );
    CHECK( GradeSigned( grade, 0.0 ) < 0.0 );

    // Solving each parameter for its handle's point recovers it.
    double lift = 0.0;
    REQUIRE( SolveLift( grade, GradeSigned( grade, 0.0 ), lift ) );
    CHECK( lift == doctest::Approx( -0.1 ) );
    double gain = 0.0;
    REQUIRE( SolveGain( grade, GradeSigned( grade, 1.0 ), gain ) );
    CHECK( gain == doctest::Approx( 0.9 ) );
    double gamma = 0.0;
    const double mid = SrgbDecode( 0.5 );
    REQUIRE( SolveGamma( grade, mid, GradeSigned( grade, mid ), gamma ) );
    CHECK( gamma == doctest::Approx( 1.3 ) );
    // Nothing can put a mid point at full white.
    CHECK_FALSE( SolveGamma( grade, mid, 1.0, gamma ) );
}

TEST_CASE( "Post widgets: white balance scales like the shader" )
{
    double scale[3];
    PostMath::WhiteBalanceScale( 0.0, 0.0, scale );
    CHECK( scale[0] == doctest::Approx( 1.0 ) );
    CHECK( scale[1] == doctest::Approx( 1.0 ) );
    CHECK( scale[2] == doctest::Approx( 1.0 ) );
    PostMath::WhiteBalanceScale( 1.0, -1.0, scale );
    CHECK( scale[0] == doctest::Approx( 1.2 ) );
    CHECK( scale[1] == doctest::Approx( 1.1 ) );
    CHECK( scale[2] == doctest::Approx( 0.8 ) );
}

TEST_CASE( "Post widgets: fog visibility is where the fog reaches 95%" )
{
    using namespace PostMath;
    // ln(20) / density: 0.035 /m sees about 86 m.
    CHECK( FogVisibility( 0.035, 0.0 ) == doctest::Approx( 85.5924 ).epsilon( 1e-4 ) );
    CHECK( FogOpacity( FogVisibility( 0.035, 0.0 ), 0.035, 0.0 ) == doctest::Approx( 0.95 ) );
    // The start distance adds on; nothing before it.
    CHECK( FogVisibility( 0.035, 10.0 ) == doctest::Approx( 95.5924 ).epsilon( 1e-4 ) );
    CHECK( FogOpacity( 5.0, 0.035, 10.0 ) == doctest::Approx( 0.0 ) );
    // No density, or a cap under 95%: unlimited.
    CHECK( std::isinf( FogVisibility( 0.0, 0.0 ) ) );
    CHECK( std::isinf( FogVisibility( 0.1, 0.0, 0.9 ) ) );
    CHECK( FogOpacity( 1000.0, 0.1, 0.0, 0.9 ) == doctest::Approx( 0.9 ) );
    // Density for a visibility inverts it.
    double density = 0.0;
    REQUIRE( FogDensityFor( 150.0, 0.0, density ) );
    CHECK( density == doctest::Approx( std::log( 20.0 ) / 150.0 ) );
    CHECK( FogVisibility( density, 0.0 ) == doctest::Approx( 150.0 ) );
    REQUIRE( FogDensityFor( 150.0, 20.0, density ) );
    CHECK( FogVisibility( density, 20.0 ) == doctest::Approx( 150.0 ) );
    CHECK_FALSE( FogDensityFor( 10.0, 20.0, density ) );
    // Above the base height the fog thins by e every 1 / falloff metres (capped at e^8 below).
    CHECK( FogHeightFactor( 10.0, 0.0, 0.1 ) == doctest::Approx( std::exp( -1.0 ) ) );
    CHECK( FogHeightFactor( -1000.0, 0.0, 0.1 ) == doctest::Approx( std::exp( 8.0 ) ) );
    CHECK( FogVisibility( 0.035, 0.0, 1.0, std::exp( -1.0 ) ) == doctest::Approx( 85.5924 * std::exp( 1.0 ) ).epsilon( 1e-4 ) );
}
