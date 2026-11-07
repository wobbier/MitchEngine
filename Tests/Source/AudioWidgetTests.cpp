#include <doctest/doctest.h>
#include <Widgets/AudioWidgets.h>
#include <cmath>
#include <limits>
#include <string>

using namespace MeWidgets;
using namespace MeWidgets::AudioMath;

TEST_CASE( "Audio widgets: gain and decibels" )
{
    CHECK( GainToDb( 1.0 ) == doctest::Approx( 0.0 ) );
    CHECK( GainToDb( 0.5 ) == doctest::Approx( -6.0206 ).epsilon( 1e-4 ) );
    CHECK( GainToDb( 0.1 ) == doctest::Approx( -20.0 ) );
    CHECK( std::isinf( GainToDb( 0.0 ) ) );
    CHECK( GainToDb( 0.0 ) < 0.0 );
    CHECK( DbToGain( -6.0 ) == doctest::Approx( 0.5011872 ) );
    CHECK( DbToGain( 6.0 ) == doctest::Approx( 1.9952623 ) );
    CHECK( DbToGain( -std::numeric_limits<double>::infinity() ) == 0.0 );
    for( double db : { -48.0, -24.0, -12.0, -6.0, -0.5, 0.0, 3.0 } )
    {
        CHECK( GainToDb( DbToGain( db ) ) == doctest::Approx( db ) );
    }
    // The meter agrees with the unit field's dB.
    CHECK( FormatValue( Unit::LinearGain, DbToGain( -6.0 ) ) == "-6.0 dB" );
}

TEST_CASE( "Audio widgets: semitones and playback rate" )
{
    CHECK( SemitonesToRatio( 0.0 ) == doctest::Approx( 1.0 ) );
    CHECK( SemitonesToRatio( 12.0 ) == doctest::Approx( 2.0 ) );
    CHECK( SemitonesToRatio( -12.0 ) == doctest::Approx( 0.5 ) );
    CHECK( SemitonesToRatio( 7.0 ) == doctest::Approx( 1.4983071 ) );
    CHECK( RatioToSemitones( 1.5 ) == doctest::Approx( 7.0195500 ) );
    for( int n = -12; n <= 12; ++n )
    {
        CHECK( RatioToSemitones( SemitonesToRatio( n ) ) == doctest::Approx( n ) );
    }
    int whole = 0;
    int cents = 0;
    // A perfect fifth (3:2) is 7 semitones and 2 cents.
    SplitCents( RatioToSemitones( 1.5 ), whole, cents );
    CHECK( whole == 7 );
    CHECK( cents == 2 );
    SplitCents( 3.6, whole, cents );
    CHECK( whole == 4 );
    CHECK( cents == -40 );
    SplitCents( -2.25, whole, cents );
    CHECK( whole == -2 );
    CHECK( cents == -25 );
    SplitCents( -0.0, whole, cents );
    CHECK( whole == 0 );
    CHECK( cents == 0 );

    // A C-based keyboard: C# D# F# G# A# are black, an octave apart the same.
    const bool black[12] = { false, true, false, true, false, false, true, false, true, false, true, false };
    for( int pc = 0; pc < 12; ++pc )
    {
        CHECK( IsBlackKey( pc ) == black[pc] );
        CHECK( IsBlackKey( pc - 12 ) == black[pc] );
        CHECK( IsBlackKey( pc + 12 ) == black[pc] );
    }
}

TEST_CASE( "Audio widgets: the level meter follows the IEC 60268-18 scale" )
{
    // 0 dB at 100 of 115 (the scale runs to +6 dB), -20 dB halfway up the 0 dB mark.
    CHECK( MeterFromDb( kMeterTopDb ) == doctest::Approx( 1.0 ) );
    CHECK( MeterFromDb( 0.0 ) == doctest::Approx( 100.0 / 115.0 ) );
    CHECK( MeterFromDb( -20.0 ) == doctest::Approx( 50.0 / 115.0 ) );
    CHECK( MeterFromDb( -30.0 ) == doctest::Approx( 30.0 / 115.0 ) );
    CHECK( MeterFromDb( -40.0 ) == doctest::Approx( 15.0 / 115.0 ) );
    CHECK( MeterFromDb( -50.0 ) == doctest::Approx( 7.5 / 115.0 ) );
    CHECK( MeterFromDb( -60.0 ) == doctest::Approx( 2.5 / 115.0 ) );
    CHECK( MeterFromDb( kMeterFloorDb ) == doctest::Approx( 0.0 ) );
    CHECK( MeterFromDb( -std::numeric_limits<double>::infinity() ) == 0.0 );
    CHECK( MeterFromDb( 20.0 ) == doctest::Approx( 1.0 ) );
    // Rising, and invertible.
    double last = -1.0;
    for( double db = -69.5; db <= 6.0; db += 0.5 )
    {
        const double t = MeterFromDb( db );
        CHECK( t > last );
        CHECK( MeterToDb( t ) == doctest::Approx( db ) );
        last = t;
    }
    CHECK( std::isinf( MeterToDb( 0.0 ) ) );
}

TEST_CASE( "Audio widgets: rolloff gain matches FMOD's models" )
{
    // Inverse: min / d, holding at min / max past the max distance.
    CHECK( RolloffGain( 0, 0.5, 1.0, 50.0 ) == doctest::Approx( 1.0 ) );
    CHECK( RolloffGain( 0, 1.0, 1.0, 50.0 ) == doctest::Approx( 1.0 ) );
    CHECK( RolloffGain( 0, 2.0, 1.0, 50.0 ) == doctest::Approx( 0.5 ) );
    CHECK( RolloffGain( 0, 4.0, 2.0, 50.0 ) == doctest::Approx( 0.5 ) );
    CHECK( RolloffGain( 0, 50.0, 1.0, 50.0 ) == doctest::Approx( 0.02 ) );
    CHECK( RolloffGain( 0, 500.0, 1.0, 50.0 ) == doctest::Approx( 0.02 ) );
    // Each doubling past min is -6 dB.
    CHECK( GainToDb( RolloffGain( 0, 8.0, 1.0, 50.0 ) ) - GainToDb( RolloffGain( 0, 4.0, 1.0, 50.0 ) ) == doctest::Approx( -6.0206 ).epsilon( 1e-4 ) );
    // Linear: full at min, silent at max.
    CHECK( RolloffGain( 1, 1.0, 1.0, 11.0 ) == doctest::Approx( 1.0 ) );
    CHECK( RolloffGain( 1, 6.0, 1.0, 11.0 ) == doctest::Approx( 0.5 ) );
    CHECK( RolloffGain( 1, 11.0, 1.0, 11.0 ) == doctest::Approx( 0.0 ) );
    CHECK( RolloffGain( 1, 20.0, 1.0, 11.0 ) == doctest::Approx( 0.0 ) );
    // Linear square: the same ramp squared.
    CHECK( RolloffGain( 2, 6.0, 1.0, 11.0 ) == doctest::Approx( 0.25 ) );
    CHECK( RolloffGain( 2, 3.5, 1.0, 11.0 ) == doctest::Approx( 0.5625 ) );
    CHECK( RolloffGain( 2, 11.0, 1.0, 11.0 ) == doctest::Approx( 0.0 ) );
    // Clamped like AudioCore's set3DMinMaxDistance: a 0 min distance is 0.001 m.
    CHECK( RolloffGain( 0, 0.002, 0.0, 50.0 ) == doctest::Approx( 0.5 ) );
    CHECK( RolloffGain( 1, 5.0, 5.0, 2.0 ) == doctest::Approx( 1.0 ) );

    CHECK( std::string( RolloffName( 0 ) ) == "Inverse" );
    CHECK( std::string( RolloffName( 1 ) ) == "Linear" );
    CHECK( std::string( RolloffName( 2 ) ) == "Linear Square" );
}

TEST_CASE( "Audio widgets: where a rolloff reaches a level" )
{
    const double half = DbToGain( -6.0 );
    // Inverse from 1 m: -6 dB at 1.995 m; from 2 m at 3.99 m.
    CHECK( RolloffDistanceFor( 0, half, 1.0, 50.0 ) == doctest::Approx( 1.9952623 ) );
    CHECK( RolloffDistanceFor( 0, half, 2.0, 50.0 ) == doctest::Approx( 3.9905246 ) );
    // ...but never past its max distance, where it holds.
    CHECK( std::isinf( RolloffDistanceFor( 0, half, 1.0, 1.5 ) ) );
    CHECK( std::isinf( RolloffDistanceFor( 0, 0.0, 1.0, 50.0 ) ) );
    CHECK( RolloffDistanceFor( 1, half, 1.0, 11.0 ) == doctest::Approx( 11.0 - half * 10.0 ) );
    CHECK( RolloffDistanceFor( 2, half, 1.0, 11.0 ) == doctest::Approx( 11.0 - std::sqrt( half ) * 10.0 ) );
    CHECK( RolloffDistanceFor( 1, 0.0, 1.0, 11.0 ) == doctest::Approx( 11.0 ) );
    CHECK( RolloffDistanceFor( 2, 1.0, 1.0, 11.0 ) == doctest::Approx( 1.0 ) );
    // Round trips through the gain.
    for( int mode = 0; mode < kRolloffCount; ++mode )
    {
        for( double gain : { 0.9, 0.5, 0.25, 0.1 } )
        {
            const double d = RolloffDistanceFor( mode, gain, 2.0, 40.0 );
            REQUIRE( std::isfinite( d ) );
            CHECK( RolloffGain( mode, d, 2.0, 40.0 ) == doctest::Approx( gain ) );
        }
    }
}
