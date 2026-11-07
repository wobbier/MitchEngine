#include <doctest/doctest.h>
#include <Widgets/CharacterWidgets.h>
#include <cmath>
#include <limits>
#include <string>

using namespace MeWidgets;
using namespace MeWidgets::CharacterMath;

TEST_CASE( "Character widgets: a capsule keeps its two caps" )
{
    // Height is end to end: never under 2 x Radius.
    CHECK( CapsuleHeight( 1.8, 0.4 ) == doctest::Approx( 1.8 ) );
    CHECK( CapsuleHeight( 0.5, 0.4 ) == doctest::Approx( 0.8 ) );
    CHECK( CapsuleHeight( 0.8, 0.4 ) == doctest::Approx( 0.8 ) );
    CHECK( CapsuleCylinder( 1.8, 0.4 ) == doctest::Approx( 1.0 ) );
    CHECK( CapsuleCylinder( 0.6, 0.4 ) == doctest::Approx( 0.0 ) );

    // A wider radius pushes the height up; a narrower one leaves it.
    double radius = 0.4;
    double height = 1.8;
    SetCapsuleRadius( 1.2, radius, height );
    CHECK( radius == doctest::Approx( 1.2 ) );
    CHECK( height == doctest::Approx( 2.4 ) );
    SetCapsuleRadius( 0.3, radius, height );
    CHECK( radius == doctest::Approx( 0.3 ) );
    CHECK( height == doctest::Approx( 2.4 ) );
    SetCapsuleRadius( -1.0, radius, height );
    CHECK( radius == doctest::Approx( 0.0 ) );

    // The silhouette shows the taller of the capsule and the person, with room to spare.
    CHECK( SilhouetteSpan( 1.0 ) == doctest::Approx( 2.5 ) );
    CHECK( SilhouetteSpan( 1.8 ) == doctest::Approx( 2.5 ) );
    CHECK( SilhouetteSpan( 2.3 ) == doctest::Approx( 3.0 ) );
    CHECK( SilhouetteSpan( 4.0 ) == doctest::Approx( 5.0 ) );
    CHECK( SilhouetteSpan( 20.0 ) == doctest::Approx( 25.0 ) );
    CHECK( SilhouetteSpan( 40.0 ) == doctest::Approx( 45.0 ) );
}

TEST_CASE( "Character widgets: the jump leaves at sqrt(2 g h) and lands after 2 v / g" )
{
    // The physics cores scale the world gravity's Y by GravityScale and keep its sign
    // (negative pulls down).
    CHECK( JumpGravity( -9.81, 1.0 ) == doctest::Approx( -9.81 ) );
    CHECK( JumpGravity( -9.81, 2.0 ) == doctest::Approx( -19.62 ) );
    CHECK( JumpGravity( 9.81, 1.0 ) == doctest::Approx( 9.81 ) );
    CHECK( JumpGravity( -9.81, 0.0 ) == doctest::Approx( 0.0 ) );

    const double g = 9.81;
    // A 1.2 m jump (CharacterController's default): 4.85 m/s up, 0.989 s in the air.
    CHECK( TakeoffSpeed( g, 1.2 ) == doctest::Approx( std::sqrt( 2.0 * 9.81 * 1.2 ) ) );
    CHECK( TakeoffSpeed( g, 1.2 ) == doctest::Approx( 4.852 ).epsilon( 0.001 ) );
    CHECK( AirTime( g, 1.2 ) == doctest::Approx( 0.989 ).epsilon( 0.001 ) );
    // Four times the height takes twice the speed and twice the time.
    CHECK( TakeoffSpeed( g, 4.8 ) == doctest::Approx( 2.0 * TakeoffSpeed( g, 1.2 ) ) );
    CHECK( AirTime( g, 4.8 ) == doctest::Approx( 2.0 * AirTime( g, 1.2 ) ) );
    // No jump, no time; no gravity, never lands (and the cores give it no takeoff speed).
    CHECK( AirTime( g, 0.0 ) == 0.0 );
    CHECK( TakeoffSpeed( 0.0, 1.2 ) == 0.0 );
    CHECK( std::isinf( AirTime( 0.0, 1.2 ) ) );
    // Pulled up (a positive gravity.y), it leaves and never comes back.
    CHECK( std::isinf( AirTime( -9.81, 1.2 ) ) );
    CHECK( JumpDistance( 6.0, -9.81, 1.2 ) == 0.0 );

    // Running at 6 m/s it lands 5.94 m on, at its apex halfway.
    const double distance = JumpDistance( 6.0, g, 1.2 );
    CHECK( distance == doctest::Approx( 6.0 * AirTime( g, 1.2 ) ) );
    CHECK( JumpHeightAt( distance * 0.5, 6.0, g, 1.2 ) == doctest::Approx( 1.2 ) );
    CHECK( JumpHeightAt( 0.0, 6.0, g, 1.2 ) == doctest::Approx( 0.0 ).epsilon( 1e-9 ) );
    CHECK( JumpHeightAt( distance, 6.0, g, 1.2 ) == doctest::Approx( 0.0 ).epsilon( 1e-9 ) );
    CHECK( JumpHeightAt( distance * 0.25, 6.0, g, 1.2 ) == doctest::Approx( 0.9 ) );
    CHECK( JumpHeightAt( -1.0, 6.0, g, 1.2 ) == 0.0 );
    CHECK( JumpHeightAt( distance + 1.0, 6.0, g, 1.2 ) == 0.0 );
    // Standing still it goes straight up.
    CHECK( JumpDistance( 0.0, g, 1.2 ) == 0.0 );
    CHECK( JumpDistance( 6.0, 0.0, 1.2 ) == 0.0 );

    int count = 0;
    const HeightMark* marks = JumpMarks( count );
    REQUIRE( count == 3 );
    CHECK( std::string( marks[0].Name ) == "Curb" );
    CHECK( marks[0].Meters == doctest::Approx( 0.15 ) );
    CHECK( marks[1].Meters == doctest::Approx( 0.75 ) );
    CHECK( marks[2].Meters == doctest::Approx( kHumanHeight ) );
}

TEST_CASE( "Character widgets: slopes and paces" )
{
    CHECK( SlopeGrade( 0.0 ) == doctest::Approx( 0.0 ) );
    CHECK( SlopeGrade( 45.0 ) == doctest::Approx( 1.0 ) );
    CHECK( SlopeGrade( 50.0 ) == doctest::Approx( 1.1918 ).epsilon( 0.001 ) );
    CHECK( std::isfinite( SlopeGrade( 90.0 ) ) );

    int count = 0;
    const SlopeMark* marks = SlopeMarks( count );
    REQUIRE( count == 3 );
    // An accessible ramp: 1 in 12.
    CHECK( std::string( marks[0].Name ) == "Ramp" );
    CHECK( SlopeGrade( marks[0].Degrees ) == doctest::Approx( 1.0 / 12.0 ) );
    CHECK( marks[1].Degrees == doctest::Approx( 35.0 ) );
    CHECK( marks[2].Degrees == doctest::Approx( 60.0 ) );

    const PaceRung* rungs = PaceRungs( count );
    REQUIRE( count == 4 );
    CHECK( std::string( rungs[0].Name ) == "Walk" );
    CHECK( rungs[0].MetersPerSecond == doctest::Approx( 1.4 ) );
    CHECK( rungs[1].MetersPerSecond == doctest::Approx( 3.0 ) );
    CHECK( rungs[2].MetersPerSecond == doctest::Approx( 5.0 ) );
    CHECK( std::string( rungs[3].Name ) == "Sprint" );
    CHECK( rungs[3].MetersPerSecond == doctest::Approx( 7.5 ) );
    for( int i = 0; i < count; ++i )
    {
        CHECK( rungs[i].MetersPerSecond < kPaceAxis );
    }
    // A float field's 5 (and 1.4f) still sit on their rungs.
    REQUIRE( PaceRungAt( 5.0 ) != nullptr );
    CHECK( std::string( PaceRungAt( 5.0 )->Name ) == "Run" );
    REQUIRE( PaceRungAt( static_cast<double>( 1.4f ) ) != nullptr );
    CHECK( std::string( PaceRungAt( static_cast<double>( 1.4f ) )->Name ) == "Walk" );
    CHECK( PaceRungAt( 4.2 ) == nullptr );

    CHECK( RoundPace( 4.237, false ) == doctest::Approx( 4.2 ) );
    CHECK( RoundPace( 4.237, true ) == doctest::Approx( 4.24 ) );
    CHECK( RoundPace( -1.0, false ) == 0.0 );
}
