#include <doctest/doctest.h>
#include "Components/Gameplay/Spline.h"
#include <vector>

// GAME-2: the C++ spline math (the C# SplineTests hold ScriptCore's copy to the same numbers).
TEST_CASE( "Spline math passes through its points and matches the reference curve" )
{
    const std::vector<Vector3> square = { Vector3( 0.f, 1.f, 0.f ), Vector3( 2.f, 1.f, 0.f ), Vector3( 2.f, 1.f, 2.f ), Vector3( 0.f, 1.f, 2.f ) };
    auto near = []( const Vector3& InA, const Vector3& InB ) { return ( InA - InB ).Length() < 2e-3f; };
    for( size_t i = 0; i < square.size(); ++i )
    {
        CHECK( near( SplineMath::Evaluate( square, false, i / 3.f ), square[i] ) );
        CHECK( near( SplineMath::Evaluate( square, true, i / 4.f ), square[i] ) );
    }
    CHECK( near( SplineMath::Evaluate( square, false, 0.5f ), Vector3( 2.25f, 1.f, 1.f ) ) );
    CHECK( near( SplineMath::Evaluate( square, false, 0.25f ), Vector3( 1.641f, 1.f, -0.141f ) ) );
    CHECK( SplineMath::GetSegmentCount( 4, false ) == 3 );
    CHECK( SplineMath::GetSegmentCount( 4, true ) == 4 );
    CHECK( SplineMath::GetSegmentCount( 1, true ) == 0 );
    CHECK( near( SplineMath::Evaluate( { Vector3( 1.f, 2.f, 3.f ) }, false, 0.7f ), Vector3( 1.f, 2.f, 3.f ) ) );
}


TEST_CASE( "Spline component: length, distance lookup and nearest point without an owner" )
{
    Spline spline;
    spline.Points = { Vector3( 0.f, 1.f, 0.f ), Vector3( 2.f, 1.f, 0.f ), Vector3( 2.f, 1.f, 2.f ), Vector3( 0.f, 1.f, 2.f ) };
    CHECK( spline.GetLength() == doctest::Approx( 6.186f ).epsilon( 0.002 ) );
    CHECK( spline.GetTAtDistance( 0.f ) == 0.f );
    CHECK( spline.GetTAtDistance( 100.f ) == 1.f );
    CHECK( spline.GetNearestT( spline.GetPoint( 0.37f ) ) == doctest::Approx( 0.37f ).epsilon( 0.01 ) );
    spline.Closed = true;
    CHECK( spline.GetLength() == doctest::Approx( 8.404f ).epsilon( 0.002 ) );
    CHECK( ( spline.GetPoint( spline.GetTAtDistance( spline.GetLength() + 1.f ) ) - spline.GetPoint( spline.GetTAtDistance( 1.f ) ) ).Length() < 1e-3f );
}
