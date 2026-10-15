#include <doctest/doctest.h>
#include "Components/Cameras/CameraSway.h"
#include <cmath>

namespace
{
    void CheckVector( const Vector3& actual, const Vector3& expected )
    {
        CHECK( actual.x == doctest::Approx( expected.x ).epsilon( 1e-5 ) );
        CHECK( actual.y == doctest::Approx( expected.y ).epsilon( 1e-5 ) );
        CHECK( actual.z == doctest::Approx( expected.z ).epsilon( 1e-5 ) );
    }
}


TEST_CASE( "CameraSway: reset and disabled motion return to the authored pose" )
{
    CameraSway sway;
    const Vector3 authoredPosition( 4.f, 2.f, -8.f );
    const Quaternion authoredRotation = Quaternion::FromEulerDegrees( Vector3( 10.f, 70.f, 5.f ) );
    Vector3 position = authoredPosition;
    Quaternion rotation = authoredRotation;
    sway.Apply( position, rotation );
    CheckVector( position, authoredPosition );
    CheckVector( rotation * Vector3::Front, authoredRotation * Vector3::Front );

    sway.Advance( 1.f );
    sway.Apply( position, rotation );
    CHECK( ( position - authoredPosition ).Length() > 0.001f );

    sway.OnDisable();
    position = authoredPosition;
    rotation = authoredRotation;
    sway.Apply( position, rotation );
    CheckVector( position, authoredPosition );
    CheckVector( rotation * Vector3::Front, authoredRotation * Vector3::Front );
}


TEST_CASE( "CameraSway: frame rate and paused frames preserve the same pose" )
{
    CameraSway coarse;
    CameraSway fine;
    for( int i = 0; i < 30; ++i ) coarse.Advance( 1.f / 30.f );
    for( int i = 0; i < 120; ++i ) fine.Advance( 1.f / 120.f );
    Vector3 coarsePosition, finePosition;
    Quaternion coarseRotation, fineRotation;
    coarse.Apply( coarsePosition, coarseRotation );
    fine.Apply( finePosition, fineRotation );
    CheckVector( coarsePosition, finePosition );
    CheckVector( coarseRotation * Vector3::Front, fineRotation * Vector3::Front );

    fine.Advance( 0.f );
    finePosition = Vector3();
    fineRotation = Quaternion();
    fine.Apply( finePosition, fineRotation );
    CheckVector( coarsePosition, finePosition );
    CheckVector( coarseRotation * Vector3::Front, fineRotation * Vector3::Front );
}


TEST_CASE( "CameraSway: offsets follow camera axes and stay bounded without drift" )
{
    CameraSway sway;
    sway.Handheld = 1.f;
    const Quaternion baseRotation = Quaternion::FromEulerDegrees( Vector3( 15.f, 90.f, 0.f ) );
    const Vector3 basePosition( 8.f, 3.f, 2.f );
    for( int i = 0; i < 3600; ++i )
    {
        sway.Advance( 1.f / 60.f );
        Vector3 localPosition;
        Quaternion localRotation;
        sway.Apply( localPosition, localRotation );
        REQUIRE( std::abs( localPosition.x ) <= sway.PositionAmplitude.x );
        REQUIRE( std::abs( localPosition.y ) <= sway.PositionAmplitude.y );
        REQUIRE( std::abs( localPosition.z ) <= sway.PositionAmplitude.z );
        const Vector3 angles = Quaternion::ToEulerAngles( localRotation );
        REQUIRE( std::abs( angles.x ) <= sway.RotationAmplitude.x + 1e-4f );
        REQUIRE( std::abs( angles.y ) <= sway.RotationAmplitude.y + 1e-4f );
        REQUIRE( std::abs( angles.z ) <= sway.RotationAmplitude.z + 1e-4f );

        Vector3 worldPosition = basePosition;
        Quaternion worldRotation = baseRotation;
        sway.Apply( worldPosition, worldRotation );
        CheckVector( worldPosition, basePosition + baseRotation * localPosition );
        CheckVector( worldRotation * Vector3::Front, baseRotation * ( localRotation * Vector3::Front ) );
    }
}


TEST_CASE( "CameraSway: settings serialize while playback starts from rest" )
{
    CameraSway source;
    source.PositionAmplitude = Vector3( 0.02f, 0.03f, 0.f );
    source.RotationAmplitude = Vector3( 0.4f, 0.2f, 0.1f );
    source.Frequency = 0.4f;
    source.Handheld = 0.7f;
    source.Advance( 3.f );
    json saved;
    source.Serialize( saved );
    CHECK( saved["Type"] == "CameraSway" );
    CHECK( saved.size() == 5 );

    CameraSway restored;
    restored.Deserialize( saved );
    CheckVector( restored.PositionAmplitude, source.PositionAmplitude );
    CheckVector( restored.RotationAmplitude, source.RotationAmplitude );
    CHECK( restored.Frequency == source.Frequency );
    CHECK( restored.Handheld == source.Handheld );
    Vector3 position;
    Quaternion rotation;
    restored.Apply( position, rotation );
    CheckVector( position, Vector3() );
    CheckVector( rotation * Vector3::Front, Vector3::Front );
}
