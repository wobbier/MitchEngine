#include <algorithm>
#include <doctest/doctest.h>
#include <Widgets/TransformWidgets.h>
#include <cmath>
#include <limits>
#include <string>

using namespace MeWidgets::TransformMath;

TEST_CASE( "Transform widgets: angles wrap into (-180, 180]" )
{
    CHECK( WrapDegrees( 0.f ) == doctest::Approx( 0.f ) );
    CHECK( WrapDegrees( 179.f ) == doctest::Approx( 179.f ) );
    CHECK( WrapDegrees( 180.f ) == doctest::Approx( 180.f ) );
    CHECK( WrapDegrees( -180.f ) == doctest::Approx( 180.f ) );
    CHECK( WrapDegrees( 181.f ) == doctest::Approx( -179.f ) );
    CHECK( WrapDegrees( -181.f ) == doctest::Approx( 179.f ) );
    CHECK( WrapDegrees( 360.f ) == doctest::Approx( 0.f ) );
    CHECK( WrapDegrees( 725.f ) == doctest::Approx( 5.f ) );
    CHECK( WrapDegrees( -725.f ) == doctest::Approx( -5.f ) );
    CHECK( WrapDegrees( std::numeric_limits<float>::quiet_NaN() ) == 0.f );
    CHECK( WrapDegrees( std::numeric_limits<float>::infinity() ) == 0.f );
    // Never "-0".
    CHECK_FALSE( std::signbit( WrapDegrees( -360.f ) ) );
}

TEST_CASE( "Transform widgets: a dial points where the pointer is" )
{
    // Screen space, y down: right 0°, up 90°, left 180°, down -90°.
    CHECK( DialDegrees( 10.f, 0.f ) == doctest::Approx( 0.f ) );
    CHECK( DialDegrees( 0.f, -10.f ) == doctest::Approx( 90.f ) );
    CHECK( DialDegrees( -10.f, 0.f ) == doctest::Approx( 180.f ) );
    CHECK( DialDegrees( 0.f, 10.f ) == doctest::Approx( -90.f ) );
    CHECK( DialDegrees( 30.f, -30.f ) == doctest::Approx( 45.f ) );
    CHECK( DialDegrees( -30.f, 30.f ) == doctest::Approx( -135.f ) );
    CHECK( DialDegrees( 0.f, 0.f ) == 0.f );

    // Whole degrees, tenths, 15° detents; snapping wraps too.
    CHECK( SnapDegrees( 44.6f, 1.f ) == doctest::Approx( 45.f ) );
    CHECK( SnapDegrees( 44.64f, 0.1f ) == doctest::Approx( 44.6f ) );
    CHECK( SnapDegrees( 37.f, 15.f ) == doctest::Approx( 30.f ) );
    CHECK( SnapDegrees( 38.f, 15.f ) == doctest::Approx( 45.f ) );
    CHECK( SnapDegrees( 179.6f, 1.f ) == doctest::Approx( 180.f ) );
    CHECK( SnapDegrees( -179.6f, 1.f ) == doctest::Approx( 180.f ) );
    CHECK( SnapDegrees( 12.34f, 0.f ) == doctest::Approx( 12.34f ) );
}

TEST_CASE( "Transform widgets: a locked scale keeps its proportions" )
{
    // 2 x 1 x 0.5, X doubled: everything doubles.
    const float before[3] = { 2.f, 1.f, 0.5f };
    float values[3] = { 4.f, 1.f, 0.5f };
    CHECK( ChangedAxis( before, values, 3 ) == 0 );
    CHECK( ProportionalScale( before, values, 3, 0 ) );
    CHECK( values[0] == doctest::Approx( 4.f ) );
    CHECK( values[1] == doctest::Approx( 2.f ) );
    CHECK( values[2] == doctest::Approx( 1.f ) );

    // Y edited: the ratio is Y's.
    float other[3] = { 2.f, 3.f, 0.5f };
    CHECK( ChangedAxis( before, other, 3 ) == 1 );
    ProportionalScale( before, other, 3, 1 );
    CHECK( other[0] == doctest::Approx( 6.f ) );
    CHECK( other[2] == doctest::Approx( 1.5f ) );

    // A zero axis stays zero when another one is edited...
    const float flat[3] = { 1.f, 0.f, 1.f };
    float flatValues[3] = { 3.f, 0.f, 1.f };
    ProportionalScale( flat, flatValues, 3, 0 );
    CHECK( flatValues[1] == doctest::Approx( 0.f ) );
    CHECK( flatValues[2] == doctest::Approx( 3.f ) );
    // ...and editing a zero axis (no ratio) moves the others by the same difference.
    float fromZero[3] = { 1.f, 0.5f, 1.f };
    ProportionalScale( flat, fromZero, 3, 1 );
    CHECK( fromZero[0] == doctest::Approx( 1.5f ) );
    CHECK( fromZero[1] == doctest::Approx( 0.5f ) );
    CHECK( fromZero[2] == doctest::Approx( 1.5f ) );

    // Negative ratios flip the others too; a bad axis changes nothing.
    float flip[3] = { -2.f, 1.f, 0.5f };
    ProportionalScale( before, flip, 3, 0 );
    CHECK( flip[1] == doctest::Approx( -1.f ) );
    CHECK( flip[2] == doctest::Approx( -0.5f ) );
    float untouched[3] = { 9.f, 9.f, 9.f };
    CHECK_FALSE( ProportionalScale( before, untouched, 3, 3 ) );
    CHECK_FALSE( ProportionalScale( before, untouched, 3, -1 ) );
    CHECK( untouched[0] == 9.f );
    CHECK( ChangedAxis( before, before, 3 ) == -1 );
}

TEST_CASE( "Transform widgets: a locked scale gesture keeps its proportions through 0" )
{
    // Typing "0.5" into X of a locked 2 x 1 x 0.5 goes through "0" on the way.
    ScaleLockGesture typed;
    float values[3] = { 2.f, 1.f, 0.5f };
    float before[3] = { 2.f, 1.f, 0.5f };
    values[0] = 0.f;
    CHECK( ScaleLockEdit( typed, before, values, 3 ) );
    CHECK( values[1] == doctest::Approx( 0.f ) );
    CHECK( values[2] == doctest::Approx( 0.f ) );
    std::copy( values, values + 3, before );
    values[0] = 0.5f;
    CHECK( ScaleLockEdit( typed, before, values, 3 ) );
    CHECK( values[0] == doctest::Approx( 0.5f ) );
    CHECK( values[1] == doctest::Approx( 0.25f ) );
    CHECK( values[2] == doctest::Approx( 0.125f ) );

    // A drag down to 0 and back up in one gesture comes back to the same ratios, and many small
    // steps don't drift.
    ScaleLockGesture drag;
    float scale[3] = { 2.f, 1.f, 0.5f };
    for( float x : { 1.5f, 1.f, 0.5f, 0.f, -0.5f, 0.f, 0.7f, 1.3f, 2.f } )
    {
        float prior[3];
        std::copy( scale, scale + 3, prior );
        scale[0] = x;
        ScaleLockEdit( drag, prior, scale, 3 );
    }
    CHECK( scale[0] == doctest::Approx( 2.f ) );
    CHECK( scale[1] == doctest::Approx( 1.f ) );
    CHECK( scale[2] == doctest::Approx( 0.5f ) );

    // A frame where nothing changed leaves the values (and the gesture) alone.
    ScaleLockGesture idle;
    const float same[3] = { 1.f, 2.f, 3.f };
    float sameValues[3] = { 1.f, 2.f, 3.f };
    CHECK_FALSE( ScaleLockEdit( idle, same, sameValues, 3 ) );
    CHECK_FALSE( idle.Held );
}

TEST_CASE( "Transform widgets: rows copy and paste vectors as text" )
{
    const float values[3] = { 1.f, 2.5f, -3.f };
    CHECK( FormatVector( values, 3 ) == "[1, 2.5, -3]" );
    const float awkward[3] = { -0.f, 0.1f, 123456.789f };
    CHECK( FormatVector( awkward, 3 ) == "[0, 0.1, 123457]" );
    const float pair[2] = { 4.f, 0.f };
    CHECK( FormatVector( pair, 2 ) == "[4, 0]" );

    float out[3] = {};
    CHECK( ParseVector( "[1, 2, 3]", out, 3 ) );
    CHECK( out[0] == 1.f );
    CHECK( out[1] == 2.f );
    CHECK( out[2] == 3.f );
    CHECK( ParseVector( "1, 2, 3", out, 3 ) );
    CHECK( ParseVector( "  4 5   6\n", out, 3 ) );
    CHECK( out[0] == 4.f );
    CHECK( out[2] == 6.f );
    CHECK( ParseVector( "[1.5,-2e1,0.25]", out, 3 ) );
    CHECK( out[1] == doctest::Approx( -20.f ) );
    CHECK( ParseVector( "(1; 2; 3)", out, 3 ) );
    // What it copies, it pastes.
    CHECK( ParseVector( FormatVector( values, 3 ), out, 3 ) );
    CHECK( out[1] == doctest::Approx( 2.5f ) );

    // Refusals leave the values alone and say why.
    float kept[3] = { 7.f, 8.f, 9.f };
    std::string error;
    CHECK_FALSE( ParseVector( "hello", kept, 3, &error ) );
    CHECK( error == "'hello' isn't a number" );
    CHECK_FALSE( ParseVector( "1, 2", kept, 3, &error ) );
    CHECK( error == "expected 3 numbers, found 2" );
    CHECK_FALSE( ParseVector( "1 2 3 4", kept, 3, &error ) );
    CHECK( error == "expected 3 numbers, found 4" );
    CHECK_FALSE( ParseVector( "", kept, 3, &error ) );
    CHECK( error == "no numbers to paste" );
    CHECK_FALSE( ParseVector( "1, 2m, 3", kept, 3, &error ) );
    CHECK( error == "'2m' isn't a number" );
    CHECK_FALSE( ParseVector( "1 inf 3", kept, 3, &error ) );
    CHECK( kept[0] == 7.f );
    CHECK( kept[2] == 9.f );
}
