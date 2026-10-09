#include "PCH.h"
#include "Rigidbody2D.h"
#include "Components/Transform.h"
#include "Physics/Box2DUtils.h"

ME_REFLECT_BEGIN( Rigidbody2D )
    ME_FIELD_NAMED( Type, "BodyType" );   // "Type" is the component type in scene JSON
    ME_FIELD( Mass ).Range( 0.f, 100000.f ).Tooltip( "Kilograms; 0 derives the mass from the colliders' densities" );
    ME_FIELD( LinearDamping ).Range( 0.f, 100.f );
    ME_FIELD( AngularDamping ).Range( 0.f, 100.f );
    ME_FIELD( GravityScale ).Range( -10.f, 10.f );
    ME_FIELD( ContinuousCollision ).Tooltip( "Sweep against other dynamic bodies too, so fast movers can't tunnel (costs more)" );
    ME_FIELD( Interpolate ).Tooltip( "Smooth the rendered pose between fixed physics steps" );
    ME_FIELD( CanSleep );
    ME_FIELD( FreezeRotation );
ME_REFLECT_END()

using namespace Box2DUtils;


Rigidbody2D::Rigidbody2D()
    : Component( "Rigidbody2D" )
{
}


void Rigidbody2D::OnDeserialize( const json& InJson )
{
    Reflection::FromJson( StaticType(), this, InJson );
}


bool Rigidbody2D::HasBody() const
{
    return IsBody( m_body );
}


void Rigidbody2D::AddForce( const Vector2& InForce, ForceMode InMode )
{
    if( !HasBody() )
    {
        return;
    }
    const b2BodyId body = Body( m_body );
    const float mass = b2Body_GetMass( body );
    switch( InMode )
    {
    case ForceMode::Force:
        b2Body_ApplyForceToCenter( body, ToB2( InForce ), true );
        break;
    case ForceMode::Impulse:
        b2Body_ApplyLinearImpulseToCenter( body, ToB2( InForce ), true );
        break;
    case ForceMode::Acceleration:
        b2Body_ApplyForceToCenter( body, ToB2( InForce * mass ), true );
        break;
    case ForceMode::VelocityChange:
        b2Body_ApplyLinearImpulseToCenter( body, ToB2( InForce * mass ), true );
        break;
    }
}


void Rigidbody2D::AddForceAtPosition( const Vector2& InForce, const Vector2& InWorldPosition, ForceMode InMode )
{
    if( !HasBody() )
    {
        return;
    }
    const b2BodyId body = Body( m_body );
    const float mass = b2Body_GetMass( body );
    const Vector2 amount = ( InMode == ForceMode::Acceleration || InMode == ForceMode::VelocityChange ) ? InForce * mass : InForce;
    if( InMode == ForceMode::Impulse || InMode == ForceMode::VelocityChange )
    {
        b2Body_ApplyLinearImpulse( body, ToB2( amount ), ToB2( InWorldPosition ), true );
    }
    else
    {
        b2Body_ApplyForce( body, ToB2( amount ), ToB2( InWorldPosition ), true );
    }
}


void Rigidbody2D::AddTorque( float InTorque, ForceMode InMode )
{
    if( !HasBody() )
    {
        return;
    }
    const b2BodyId body = Body( m_body );
    const float inertia = b2Body_GetRotationalInertia( body );
    switch( InMode )
    {
    case ForceMode::Force:
        b2Body_ApplyTorque( body, InTorque, true );
        break;
    case ForceMode::Impulse:
        b2Body_ApplyAngularImpulse( body, InTorque, true );
        break;
    case ForceMode::Acceleration:
        b2Body_ApplyTorque( body, InTorque * inertia, true );
        break;
    case ForceMode::VelocityChange:
        b2Body_ApplyAngularImpulse( body, InTorque * inertia, true );
        break;
    }
}


Vector2 Rigidbody2D::GetVelocity() const
{
    return HasBody() ? FromB2( b2Body_GetLinearVelocity( Body( m_body ) ) ) : Vector2();
}


void Rigidbody2D::SetVelocity( const Vector2& InVelocity )
{
    if( HasBody() )
    {
        b2Body_SetLinearVelocity( Body( m_body ), ToB2( InVelocity ) );
    }
}


float Rigidbody2D::GetAngularVelocity() const
{
    return HasBody() ? b2Body_GetAngularVelocity( Body( m_body ) ) : 0.f;
}


void Rigidbody2D::SetAngularVelocity( float InRadiansPerSecond )
{
    if( HasBody() )
    {
        b2Body_SetAngularVelocity( Body( m_body ), InRadiansPerSecond );
    }
}


void Rigidbody2D::MoveTo( const Vector2& InPosition, float InAngleDegrees )
{
    // Applied by the core at the next fixed step (it knows the step length).
    m_hasPendingMove = true;
    m_movePosition = InPosition;
    m_moveAngle = InAngleDegrees * 3.14159265358979f / 180.f;
}


void Rigidbody2D::Teleport( const Vector2& InPosition, float InAngleDegrees )
{
    const float radians = InAngleDegrees * 3.14159265358979f / 180.f;
    if( HasBody() )
    {
        b2Body_SetTransform( Body( m_body ), ToB2( InPosition ), b2MakeRot( radians ) );
    }
    // The Transform jumps too, so the core restarts interpolation there instead of sliding.
    if( Parent )
    {
        if( Transform* transform = Parent->TryGetComponent<Transform>() )
        {
            transform->SetWorldPosition( Vector3( InPosition.x, InPosition.y, transform->GetWorldPosition().z ) );
            transform->SetWorldRotation( WithAngle( transform->GetWorldRotation(), radians ) );
        }
    }
    m_hasPendingMove = false;
}


float Rigidbody2D::GetBodyMass() const
{
    return HasBody() ? b2Body_GetMass( Body( m_body ) ) : Mass;
}


bool Rigidbody2D::IsSleeping() const
{
    return HasBody() && !b2Body_IsAwake( Body( m_body ) );
}


void Rigidbody2D::WakeUp()
{
    if( HasBody() )
    {
        b2Body_SetAwake( Body( m_body ), true );
    }
}
