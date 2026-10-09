#include "PCH.h"
#include "Rigidbody.h"
#include "Components/Transform.h"
#include "Physics/Box3DUtils.h"

ME_REFLECT_ENUM( BodyType, { { "Static", BodyType::Static }, { "Kinematic", BodyType::Kinematic }, { "Dynamic", BodyType::Dynamic } } )

ME_REFLECT_BEGIN( Rigidbody )
    ME_FIELD_NAMED( Type, "BodyType" );   // "Type" is the component type in scene JSON
    ME_FIELD( Mass ).Range( 0.f, 100000.f ).Tooltip( "Kilograms; 0 derives the mass from the colliders' densities" );
    ME_FIELD( LinearDamping ).Range( 0.f, 100.f );
    ME_FIELD( AngularDamping ).Range( 0.f, 100.f );
    ME_FIELD( GravityScale ).Range( -10.f, 10.f );
    ME_FIELD( ContinuousCollision ).Tooltip( "Sweep fast movers so they can't pass through thin geometry (costs more)" );
    ME_FIELD( Interpolate ).Tooltip( "Smooth the rendered pose between fixed physics steps" );
    ME_FIELD( CanSleep );
    ME_FIELD( LockPositionX ).Category( "Constraints" );
    ME_FIELD( LockPositionY ).Category( "Constraints" );
    ME_FIELD( LockPositionZ ).Category( "Constraints" );
    ME_FIELD( LockRotationX ).Category( "Constraints" );
    ME_FIELD( LockRotationY ).Category( "Constraints" );
    ME_FIELD( LockRotationZ ).Category( "Constraints" );
ME_REFLECT_END()

using namespace Box3DUtils;


Rigidbody::Rigidbody()
    : Component( "Rigidbody" )
{
}


void Rigidbody::OnDeserialize( const json& InJson )
{
    Reflection::FromJson( StaticType(), this, InJson );
}


bool Rigidbody::HasBody() const
{
    return IsBody( m_body );
}


void Rigidbody::AddForce( const Vector3& InForce, ForceMode InMode )
{
    if( !HasBody() )
    {
        return;
    }
    const b3BodyId body = Body( m_body );
    const float mass = b3Body_GetMass( body );
    switch( InMode )
    {
    case ForceMode::Force:
        b3Body_ApplyForceToCenter( body, ToB3( InForce ), true );
        break;
    case ForceMode::Impulse:
        b3Body_ApplyLinearImpulseToCenter( body, ToB3( InForce ), true );
        break;
    case ForceMode::Acceleration:
        b3Body_ApplyForceToCenter( body, ToB3( InForce * mass ), true );
        break;
    case ForceMode::VelocityChange:
        b3Body_ApplyLinearImpulseToCenter( body, ToB3( InForce * mass ), true );
        break;
    }
}


void Rigidbody::AddForceAtPosition( const Vector3& InForce, const Vector3& InWorldPosition, ForceMode InMode )
{
    if( !HasBody() )
    {
        return;
    }
    const b3BodyId body = Body( m_body );
    const float mass = b3Body_GetMass( body );
    const bool impulse = InMode == ForceMode::Impulse || InMode == ForceMode::VelocityChange;
    const Vector3 amount = ( InMode == ForceMode::Acceleration || InMode == ForceMode::VelocityChange ) ? InForce * mass : InForce;
    if( impulse )
    {
        b3Body_ApplyLinearImpulse( body, ToB3( amount ), ToB3( InWorldPosition ), true );
    }
    else
    {
        b3Body_ApplyForce( body, ToB3( amount ), ToB3( InWorldPosition ), true );
    }
}


void Rigidbody::AddTorque( const Vector3& InTorque, ForceMode InMode )
{
    if( !HasBody() )
    {
        return;
    }
    const b3BodyId body = Body( m_body );
    switch( InMode )
    {
    case ForceMode::Force:
        b3Body_ApplyTorque( body, ToB3( InTorque ), true );
        break;
    case ForceMode::Impulse:
        b3Body_ApplyAngularImpulse( body, ToB3( InTorque ), true );
        break;
    case ForceMode::Acceleration:
        // Mass-independent: integrate over one fixed step.
        SetAngularVelocity( GetAngularVelocity() + InTorque * ( 1.f / 60.f ) );
        break;
    case ForceMode::VelocityChange:
        SetAngularVelocity( GetAngularVelocity() + InTorque );
        break;
    }
}


Vector3 Rigidbody::GetVelocity() const
{
    return HasBody() ? FromB3( b3Body_GetLinearVelocity( Body( m_body ) ) ) : Vector3();
}


void Rigidbody::SetVelocity( const Vector3& InVelocity )
{
    if( HasBody() )
    {
        b3Body_SetLinearVelocity( Body( m_body ), ToB3( InVelocity ) );
    }
}


Vector3 Rigidbody::GetAngularVelocity() const
{
    return HasBody() ? FromB3( b3Body_GetAngularVelocity( Body( m_body ) ) ) : Vector3();
}


void Rigidbody::SetAngularVelocity( const Vector3& InVelocity )
{
    if( HasBody() )
    {
        b3Body_SetAngularVelocity( Body( m_body ), ToB3( InVelocity ) );
    }
}


Vector3 Rigidbody::GetPointVelocity( const Vector3& InWorldPoint ) const
{
    return HasBody() ? FromB3( b3Body_GetWorldPointVelocity( Body( m_body ), ToB3( InWorldPoint ) ) ) : Vector3();
}


void Rigidbody::MoveTo( const Vector3& InPosition, const Quaternion& InRotation )
{
    // Applied by the core at the next fixed step (it knows the step length).
    m_hasPendingMove = true;
    m_movePosition = InPosition;
    m_moveRotation = InRotation;
}


void Rigidbody::Teleport( const Vector3& InPosition, const Quaternion& InRotation )
{
    if( HasBody() )
    {
        b3Body_SetTransform( Body( m_body ), ToB3( InPosition ), ToB3( InRotation ) );
    }
    // The Transform jumps too, so the core restarts interpolation there instead of sliding.
    if( Parent )
    {
        if( Transform* transform = Parent->TryGetComponent<Transform>() )
        {
            transform->SetWorldPosition( InPosition );
            transform->SetWorldRotation( InRotation );
        }
    }
    m_hasPendingMove = false;
}


float Rigidbody::GetBodyMass() const
{
    return HasBody() ? b3Body_GetMass( Body( m_body ) ) : Mass;
}


Vector3 Rigidbody::GetCenterOfMass() const
{
    return HasBody() ? FromB3( b3Body_GetWorldCenter( Body( m_body ) ) ) : Vector3();
}


bool Rigidbody::IsSleeping() const
{
    return HasBody() && !b3Body_IsAwake( Body( m_body ) );
}


void Rigidbody::WakeUp()
{
    if( HasBody() )
    {
        b3Body_SetAwake( Body( m_body ), true );
    }
}


void Rigidbody::Sleep()
{
    if( HasBody() )
    {
        b3Body_SetAwake( Body( m_body ), false );
    }
}


void Rigidbody::SetMass( float InMass )
{
    // Mass 0 used to mean "static" with the old physics backend.
    if( InMass <= 0.f )
    {
        Type = BodyType::Static;
        return;
    }
    Mass = InMass;
}
