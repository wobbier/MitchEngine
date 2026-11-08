#include "PCH.h"
#include "PhysicsJoint.h"

ME_REFLECT_ENUM( JointType, { { "Fixed", JointType::Fixed }, { "Hinge", JointType::Hinge }, { "BallSocket", JointType::BallSocket }, { "Slider", JointType::Slider }, { "Distance", JointType::Distance } } )

ME_REFLECT_BEGIN( PhysicsJoint )
    ME_FIELD_NAMED( Type, "JointType" );   // "Type" is the component type in scene JSON
    ME_FIELD( ConnectedBody ).Tooltip( "Entity with a Rigidbody; empty attaches to the world" );
    ME_FIELD( Anchor ).Unit( Reflection::Unit::Meters ).Tooltip( "Local to this entity" );
    ME_FIELD( ConnectedAnchor ).Unit( Reflection::Unit::Meters ).Tooltip( "Distance joints: the other end, local to the connected body (or a world position)" );
    ME_FIELD( Axis ).Tooltip( "Hinge / slider axis, local to this entity" );
    ME_FIELD( CollideConnected );
    ME_FIELD( UseLimits ).Category( "Limits" );
    ME_FIELD( LowerLimit ).Category( "Limits" );
    ME_FIELD( UpperLimit ).Category( "Limits" );
    ME_FIELD( UseMotor ).Category( "Motor" );
    ME_FIELD( MotorSpeed ).Category( "Motor" );
    ME_FIELD( MaxMotorForce ).Category( "Motor" ).Range( 0.f, 1000000.f ).Log().SoftRange( 0.f, 100000.f );
    ME_FIELD( UseSpring ).Category( "Spring" );
    ME_FIELD( SpringFrequency ).Category( "Spring" ).Range( 0.f, 60.f ).Unit( Reflection::Unit::Hertz ).Log().SoftRange( 0.f, 20.f );
    ME_FIELD( SpringDamping ).Category( "Spring" ).Range( 0.f, 2.f );
    ME_FIELD( Distance ).Range( 0.f, 1000.f ).Unit( Reflection::Unit::Meters ).Log().SoftRange( 0.f, 50.f ).Tooltip( "Distance joints; 0 keeps the distance at creation" );
    ME_FIELD( BreakForce ).Range( 0.f, 1000000.f ).Unit( Reflection::Unit::Newtons ).Log().SoftRange( 0.f, 100000.f ).Tooltip( "0 = unbreakable" );
ME_REFLECT_END()


PhysicsJoint::PhysicsJoint()
    : Component( "PhysicsJoint" )
{
}


void PhysicsJoint::OnDeserialize( const json& InJson )
{
    Reflection::FromJson( StaticType(), this, InJson );
    m_broken = false;
}
