#include <doctest/doctest.h>
#include <Widgets/PhysicsWidgets.h>
#include <Widgets/Juice.h>
#include <cmath>
#include <limits>
#include <string>

using namespace MeWidgets;
using namespace MeWidgets::PhysicsMath;

TEST_CASE( "Physics widgets: the mass ladder maps kilograms in log space" )
{
    int count = 0;
    const MassRung* rungs = MassRungs( count );
    REQUIRE( count == 7 );
    CHECK( std::string( rungs[0].Name ) == "Feather" );
    CHECK( rungs[0].Kilograms == doctest::Approx( 0.01 ) );
    CHECK( rungs[2].Kilograms == doctest::Approx( 4.0 ) );
    CHECK( rungs[3].Kilograms == doctest::Approx( 70.0 ) );
    CHECK( std::string( rungs[6].Name ) == "Truck" );
    CHECK( rungs[6].Kilograms == doctest::Approx( 15000.0 ) );

    // Ends, and the rungs rise along the strip inside it.
    CHECK( MassToLadder( kLadderMinKg ) == doctest::Approx( 0.0 ) );
    CHECK( MassToLadder( kLadderMaxKg ) == doctest::Approx( 1.0 ) );
    CHECK( MassToLadder( 0.0 ) == doctest::Approx( 0.0 ) );
    CHECK( MassToLadder( 1e9 ) == doctest::Approx( 1.0 ) );
    double last = 0.0;
    for( int i = 0; i < count; ++i )
    {
        const double t = MassToLadder( rungs[i].Kilograms );
        CHECK( t > last );
        CHECK( t < 1.0 );
        CHECK( MassFromLadder( t ) == doctest::Approx( rungs[i].Kilograms ) );
        last = t;
    }
    // Log: each decade is the same length.
    CHECK( MassToLadder( 10.0 ) - MassToLadder( 1.0 ) == doctest::Approx( MassToLadder( 1000.0 ) - MassToLadder( 100.0 ) ) );
    // Pointing gives two significant digits.
    CHECK( RoundMass( 4.237 ) == doctest::Approx( 4.2 ) );
    CHECK( RoundMass( 0.3749 ) == doctest::Approx( 0.37 ) );
    CHECK( RoundMass( 1534.0 ) == doctest::Approx( 1500.0 ) );
    CHECK( RoundMass( 0.0 ) == 0.0 );
}

TEST_CASE( "Physics widgets: bounce heights fall by e^2 each bounce" )
{
    CHECK( BounceRatio( 0.6 ) == doctest::Approx( 0.36 ) );
    CHECK( BounceRatio( 0.0 ) == 0.0 );
    CHECK( BounceRatio( 1.5 ) == doctest::Approx( 1.0 ) );
    CHECK( BounceHeight( 2.0, 0.5, 0 ) == doctest::Approx( 2.0 ) );
    CHECK( BounceHeight( 2.0, 0.5, 1 ) == doctest::Approx( 0.5 ) );
    CHECK( BounceHeight( 2.0, 0.5, 3 ) == doctest::Approx( 2.0 * std::pow( 0.5, 6 ) ) );

    // The trajectory: dropped from 1, on the floor after the fall time, apexes at h_n.
    const double fall = 0.4;
    const double e = 0.6;
    CHECK( BallHeight( 0.0, e, fall ) == doctest::Approx( 1.0 ) );
    CHECK( BallHeight( fall, e, fall ) == doctest::Approx( 0.0 ).epsilon( 1e-9 ) );
    CHECK( BallHeight( fall * 0.5, e, fall ) == doctest::Approx( 0.75 ) );
    for( int n = 1; n <= 4; ++n )
    {
        const double apex = BounceApexTime( n, e, fall );
        CHECK( BallHeight( apex, e, fall ) == doctest::Approx( BounceHeight( 1.0, e, n ) ) );
        // The apex is the highest point of that bounce.
        CHECK( BallHeight( apex + 0.01, e, fall ) < BallHeight( apex, e, fall ) );
        CHECK( BallHeight( apex - 0.01, e, fall ) < BallHeight( apex, e, fall ) );
    }
    // Bounce n ends where bounce n + 1 starts: on the floor between two apexes.
    const double landing = fall + 2.0 * fall * e;
    CHECK( BallHeight( landing, e, fall ) == doctest::Approx( 0.0 ).epsilon( 1e-9 ) );
    // No restitution: it lands and stays; full restitution: every bounce reaches the drop height.
    CHECK( BallHeight( 2.0, 0.0, fall ) == 0.0 );
    CHECK( BallHeight( BounceApexTime( 3, 1.0, fall ), 1.0, fall ) == doctest::Approx( 1.0 ) );
}

TEST_CASE( "Physics widgets: slide angle is atan(friction)" )
{
    CHECK( SlideAngleDegrees( 0.0 ) == doctest::Approx( 0.0 ) );
    CHECK( SlideAngleDegrees( 1.0 ) == doctest::Approx( 45.0 ) );
    CHECK( SlideAngleDegrees( 0.6 ) == doctest::Approx( 30.9638 ).epsilon( 1e-5 ) );
    CHECK( SlideAngleDegrees( 2.0 ) == doctest::Approx( 63.4349 ).epsilon( 1e-5 ) );
    CHECK( FrictionForAngle( 45.0 ) == doctest::Approx( 1.0 ) );
    CHECK( FrictionForAngle( 31.0 ) == doctest::Approx( 0.600861 ).epsilon( 1e-5 ) );
    for( double mu : { 0.05, 0.3, 0.6, 1.2, 1.9 } )
    {
        CHECK( FrictionForAngle( SlideAngleDegrees( mu ) ) == doctest::Approx( mu ) );
    }
}

TEST_CASE( "Physics widgets: damping decays like Box3D's per-substep factor" )
{
    const double h = 1.0 / 240.0;
    // One substep: v *= 1 / (1 + h d) (solver.c).
    CHECK( DampingStepFactor( 2.0, h ) == doctest::Approx( 1.0 / ( 1.0 + 2.0 / 240.0 ) ) );
    // Continuous: a whole number of substeps compounds the factor exactly.
    double v = 1.0;
    for( int i = 0; i < 240; ++i )
    {
        v *= DampingStepFactor( 2.0, h );
    }
    CHECK( DampedSpeed( 2.0, 1.0, h ) == doctest::Approx( v ) );
    // Close to exp(-d t) for small h d; exactly it without substeps.
    CHECK( DampedSpeed( 2.0, 1.0, h ) == doctest::Approx( std::exp( -2.0 ) ).epsilon( 0.01 ) );
    CHECK( DampedSpeed( 2.0, 1.0, 0.0 ) == doctest::Approx( std::exp( -2.0 ) ) );
    CHECK( DampedSpeed( 0.0, 3.0, h ) == doctest::Approx( 1.0 ) );

    // Half-life: speed halves there, and the inverse gives the damping back.
    const double half = DampingHalfLife( 0.5, h );
    CHECK( DampedSpeed( 0.5, half, h ) == doctest::Approx( 0.5 ) );
    CHECK( half == doctest::Approx( std::log( 2.0 ) / 0.5 ).epsilon( 0.01 ) );
    CHECK( DampingForHalfLife( half, h ) == doctest::Approx( 0.5 ) );
    CHECK( std::isinf( DampingHalfLife( 0.0, h ) ) );
    CHECK( DampingForHalfLife( std::numeric_limits<double>::infinity(), h ) == 0.0 );
    for( double t : { 0.05, 0.25, 1.0, 2.5 } )
    {
        CHECK( DampingHalfLife( DampingForHalfLife( t, h ), h ) == doctest::Approx( t ) );
    }

    // The graph's time axis fits the half-life with room to spare, up to 30 s.
    CHECK( DampingAxisSeconds( 1.3 ) == 3.0 );
    CHECK( DampingAxisSeconds( 2.4 ) == 3.0 );
    CHECK( DampingAxisSeconds( 2.5 ) == 5.0 );
    CHECK( DampingAxisSeconds( DampingHalfLife( 0.05, h ) ) == 20.0 );
    CHECK( DampingAxisSeconds( 25.0 ) == kDampingMaxAxis );
    CHECK( DampingAxisSeconds( std::numeric_limits<double>::infinity() ) == kDampingMaxAxis );
}

TEST_CASE( "Juice: loop clocks run on Juice time and stop when motion is off" )
{
    Juice::Reset();
    Juice::SetReduceMotion( false );
    Juice::SetLevel( Juice::Level::Full );
    const ImGuiID id = 0x5151;
    CHECK( Juice::LoopTime( id, 1.f ) == doctest::Approx( 0.f ) );
    Juice::Update( 0.25f );
    CHECK( Juice::LoopTime( id, 1.f ) == doctest::Approx( 0.25f ) );
    Juice::Update( 0.85f );
    CHECK( Juice::LoopTime( id, 1.f ) == doctest::Approx( 0.1f ).epsilon( 1e-4 ) );
    Juice::Restart( id );
    CHECK( Juice::LoopTime( id, 1.f ) == doctest::Approx( 0.f ) );
    Juice::SetLevel( Juice::Level::Off );
    CHECK( Juice::LoopTime( id, 1.f ) < 0.f );
    Juice::SetLevel( Juice::Level::Full );
    Juice::SetReduceMotion( true );
    CHECK( Juice::LoopTime( id, 1.f ) < 0.f );
    Juice::SetReduceMotion( false );
    Juice::Reset();
}
