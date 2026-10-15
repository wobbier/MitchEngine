#include "PCH.h"
#include "CameraSway.h"
#include <algorithm>
#include <cmath>

ME_REFLECT_BEGIN( CameraSway )
    ME_FIELD( PositionAmplitude ).Display( "Position Amplitude (m)" ).Speed( 0.001f )
        .Tooltip( "Maximum sideways, vertical and forward drift in the camera's local axes" );
    ME_FIELD( RotationAmplitude ).Display( "Rotation Amplitude (deg)" ).Speed( 0.01f )
        .Tooltip( "Maximum pitch, yaw and roll around the camera's local axes" );
    ME_FIELD( Frequency ).Display( "Frequency (Hz)" ).Range( 0.f, 5.f ).Speed( 0.01f )
        .Tooltip( "Base sway speed; lower values feel like a slow breeze. Zero freezes the motion" );
    ME_FIELD( Handheld ).Range( 0.f, 1.f ).Speed( 0.01f )
        .Tooltip( "Blend in faster, small hand movements; zero gives only slow drift" );
ME_REFLECT_END()


CameraSway::CameraSway()
    : Component( "CameraSway" )
{
}


void CameraSway::Advance( float InDeltaTime )
{
    if( std::isfinite( InDeltaTime ) && std::isfinite( Frequency ) )
    {
        m_phase += double( std::max( InDeltaTime, 0.f ) ) * std::max( Frequency, 0.f ) * 6.283185307179586;
    }
}


void CameraSway::Reset()
{
    m_phase = 0.0;
}


void CameraSway::Apply( Vector3& InOutPosition, Quaternion& InOutRotation ) const
{
    const double detail = std::clamp( Handheld, 0.f, 1.f ) * 0.25;
    const auto wave = [&]( double slow, double secondary, double fast ) {
        const double drift = 0.7 * std::sin( m_phase * slow ) + 0.3 * std::sin( m_phase * secondary );
        return float( ( 1.0 - detail ) * drift + detail * std::sin( m_phase * fast ) );
    };
    const Vector3 offset( PositionAmplitude.x * wave( 0.83, 1.71, 7.13 ),
        PositionAmplitude.y * wave( 1.0, 1.37, 8.47 ),
        PositionAmplitude.z * wave( 0.67, 1.93, 6.79 ) );
    const Vector3 angles( RotationAmplitude.x * wave( 0.91, 1.61, 8.83 ),
        RotationAmplitude.y * wave( 0.73, 1.43, 7.67 ),
        RotationAmplitude.z * wave( 0.59, 1.83, 9.31 ) );

    InOutPosition += InOutRotation * offset;
    InOutRotation = ( InOutRotation * Quaternion::FromEulerDegrees( angles ) ).Normalized();
}


void CameraSway::OnDisable()
{
    Reset();
}
