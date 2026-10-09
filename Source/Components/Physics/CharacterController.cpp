#include "PCH.h"
#include "CharacterController.h"
#include "Components/Transform.h"
#include "Physics/Box3DUtils.h"

ME_REFLECT_BEGIN( CharacterController )
    ME_FIELD( Center );
    ME_FIELD( Radius ).Range( 0.05f, 10.f );
    ME_FIELD( Height ).Range( 0.1f, 20.f ).Tooltip( "End to end, including the caps" );
    ME_FIELD( MaxSpeed ).Category( "Movement" ).Range( 0.f, 100.f );
    ME_FIELD( Acceleration ).Category( "Movement" ).Range( 0.f, 500.f );
    ME_FIELD( AirControl ).Category( "Movement" ).Range( 0.f, 1.f );
    ME_FIELD( JumpHeight ).Category( "Movement" ).Range( 0.f, 20.f );
    ME_FIELD( GravityScale ).Category( "Movement" ).Range( 0.f, 10.f );
    ME_FIELD( SlopeLimit ).Category( "Collision" ).Range( 0.f, 89.f ).Tooltip( "Steeper surfaces count as walls" );
    ME_FIELD( GroundSnap ).Category( "Collision" ).Range( 0.f, 2.f ).Tooltip( "Follow the ground down steps and slopes" );
    ME_FIELD( PushStrength ).Category( "Collision" ).Range( 0.f, 20.f );
ME_REFLECT_END()


CharacterController::CharacterController()
    : Component( "CharacterController" )
{
}


void CharacterController::OnDeserialize( const json& InJson )
{
    Reflection::FromJson( StaticType(), this, InJson );
}


void CharacterController::SetMoveInput( const Vector3& InDirection )
{
    Vector3 direction( InDirection.x, 0.f, InDirection.z );
    if( direction.LengthSquared() > 1.f )
    {
        direction = direction.Normalized();
    }
    m_moveInput = direction;
}


void CharacterController::Move( const Vector3& InDisplacement )
{
    m_pendingMove += InDisplacement;
}


void CharacterController::Jump()
{
    m_wantsJump = true;
}


void CharacterController::Teleport( const Vector3& InPosition, const Quaternion& InRotation )
{
    m_position = InPosition - Center;
    m_previousPosition = m_position;
    m_appliedCenter = m_position;
    m_hasPosition = true;
    m_velocity = Vector3();
    if( Parent )
    {
        if( Transform* transform = Parent->TryGetComponent<Transform>() )
        {
            transform->SetWorldPosition( InPosition );
            transform->SetWorldRotation( InRotation );
        }
    }
    if( Box3DUtils::IsBody( m_body ) )
    {
        b3Body_SetTransform( Box3DUtils::Body( m_body ), Box3DUtils::ToB3( m_position ), b3Quat_identity );
    }
}


Vector3 CharacterController::GetPosition() const
{
    return m_position + Center;
}
