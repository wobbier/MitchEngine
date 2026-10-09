#pragma once
#include "ECS/Component.h"
#include "ECS/ComponentDetail.h"
#include "ECS/EntityHandle.h"
#include "Math/Vector2.h"
#include <cstdint>

enum class JointType2D : uint8_t
{
    Fixed = 0,      // welds the two bodies together
    Hinge,          // rotation about the anchor (doors, flippers, pendulums)
    Slider,         // translation along Axis (pistons, elevators)
    Distance,       // keeps the anchors at a distance (ropes, springs)
    Wheel,          // a suspended wheel: free spin, spring travel along Axis (vehicles)
};

// Connects this entity's Rigidbody2D to ConnectedBody's (or to the world when it's empty). Anchor and
// Axis are in this entity's local XY space; the other body is joined where Anchor sits when the joint
// is created. Distance joints instead join Anchor to ConnectedAnchor (local to ConnectedBody, or a
// world XY position). Limits are degrees for hinges and metres otherwise; motor speeds are deg/s
// (hinges, wheels) or m/s.
class PhysicsJoint2D
    : public Component<PhysicsJoint2D>
{
    ME_REFLECTABLE( PhysicsJoint2D )
    friend class Physics2DCore;
public:
    PhysicsJoint2D();

    JointType2D Type = JointType2D::Hinge;
    EntityHandle ConnectedBody;
    Vector2 Anchor = Vector2( 0.f, 0.f );
    Vector2 ConnectedAnchor = Vector2( 0.f, 0.f );
    Vector2 Axis = Vector2( 0.f, 1.f );
    bool CollideConnected = false;

    bool UseLimits = false;
    float LowerLimit = -45.f;
    float UpperLimit = 45.f;

    bool UseMotor = false;
    float MotorSpeed = 90.f;
    float MaxMotorForce = 100.f;    // N or N m

    bool UseSpring = false;
    float SpringFrequency = 4.f;    // Hz
    float SpringDamping = 0.5f;     // damping ratio

    float Distance = 0.f;           // Distance joints; 0 keeps the distance at creation
    float BreakForce = 0.f;         // N (and N m); 0 = unbreakable

    bool IsBroken() const { return m_broken; }

private:
    uint64_t m_joint = 0;
    uint64_t m_signature = 0;
    bool m_broken = false;

    void OnDeserialize( const json& InJson ) override;
};
ME_REGISTER_COMPONENT_FOLDER( PhysicsJoint2D, "Physics 2D" )
