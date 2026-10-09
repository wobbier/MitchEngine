#include "PCH.h"
#include "CharacterController2D.h"
#include "Components/Transform.h"
#include "Physics/Box2DUtils.h"
#include <algorithm>

ME_REFLECT_BEGIN( CharacterController2D )
    ME_FIELD( Offset );
    ME_FIELD( Radius ).Range( 0.05f, 10.f );
    ME_FIELD( Height ).Range( 0.1f, 20.f ).Tooltip( "End to end, including the caps" );
    ME_FIELD( MaxSpeed ).Category( "Movement" ).Range( 0.f, 100.f );
    ME_FIELD( Acceleration ).Category( "Movement" ).Range( 0.f, 500.f );
    ME_FIELD( AirControl ).Category( "Movement" ).Range( 0.f, 1.f );
    ME_FIELD( JumpHeight ).Category( "Movement" ).Range( 0.f, 20.f );
    ME_FIELD( GravityScale ).Category( "Movement" ).Range( 0.f, 10.f );
    ME_FIELD( CoyoteTime ).Category( "Movement" ).Range( 0.f, 1.f ).Tooltip( "Seconds a jump still works after leaving the ground" );
    ME_FIELD( SlopeLimit ).Category( "Collision" ).Range( 0.f, 89.f ).Tooltip( "Steeper surfaces count as walls" );
    ME_FIELD( GroundSnap ).Category( "Collision" ).Range( 0.f, 2.f ).Tooltip( "Follow the ground down steps and slopes" );
    ME_FIELD( PushStrength ).Category( "Collision" ).Range( 0.f, 20.f );
ME_REFLECT_END()


CharacterController2D::CharacterController2D()
    : Component( "CharacterController2D" )
{
}


void CharacterController2D::OnDeserialize( const json& InJson )
{
    Reflection::FromJson( StaticType(), this, InJson );
}


void CharacterController2D::SetMoveInput( float InHorizontal )
{
    m_moveInput = std::clamp( InHorizontal, -1.f, 1.f );
}


void CharacterController2D::Move( const Vector2& InDisplacement )
{
    m_pendingMove = m_pendingMove + InDisplacement;
}


void CharacterController2D::Jump()
{
    m_wantsJump = true;
}


void CharacterController2D::Teleport( const Vector2& InPosition )
{
    m_position = InPosition - Offset;
    m_previousPosition = m_position;
    m_appliedCenter = m_position;
    m_hasPosition = true;
    m_velocity = Vector2();
    if( Parent )
    {
        if( Transform* transform = Parent->TryGetComponent<Transform>() )
        {
            transform->SetWorldPosition( Vector3( InPosition.x, InPosition.y, transform->GetWorldPosition().z ) );
        }
    }
    if( Box2DUtils::IsBody( m_body ) )
    {
        b2Body_SetTransform( Box2DUtils::Body( m_body ), Box2DUtils::ToB2( m_position ), b2Rot_identity );
    }
}


Vector2 CharacterController2D::GetPosition() const
{
    return m_position + Offset;
}
