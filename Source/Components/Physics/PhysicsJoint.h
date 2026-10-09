#pragma once
#include "ECS/Component.h"
#include "ECS/ComponentDetail.h"
#include "ECS/EntityHandle.h"
#include "Math/Vector3.h"
#include <cstdint>

enum class JointType : uint8_t
{
    Fixed = 0,      // welds the two bodies together
    Hinge,          // rotation about Axis (doors, wheels, pendulums)
    BallSocket,     // free rotation about the anchor (ragdoll shoulders, chains)
    Slider,         // translation along Axis (pistons, drawers)
    Distance,       // keeps the anchors at a distance (ropes, springs)
};

// Connects this entity's Rigidbody to ConnectedBody's (or to the world when it's empty). Anchor and
// Axis are in this entity's local space; the other bodies are joined where Anchor sits when the joint
// is created. Distance joints instead join Anchor to ConnectedAnchor (local to ConnectedBody, or a
// world position). Limits are degrees for hinges, metres for sliders and distance joints; motor
// speeds are deg/s or m/s.
class PhysicsJoint
    : public Component<PhysicsJoint>
{
    ME_REFLECTABLE( PhysicsJoint )
    friend class PhysicsCore;
public:
    PhysicsJoint();

    JointType Type = JointType::Hinge;
    EntityHandle ConnectedBody;
    Vector3 Anchor = Vector3( 0.f, 0.f, 0.f );
    Vector3 ConnectedAnchor = Vector3( 0.f, 0.f, 0.f );
    Vector3 Axis = Vector3( 0.f, 1.f, 0.f );
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
    float BreakForce = 0.f;         // N; 0 = unbreakable

    bool IsBroken() const { return m_broken; }

private:
    uint64_t m_joint = 0;
    uint64_t m_signature = 0;
    bool m_broken = false;

    void OnDeserialize( const json& InJson ) override;
};
ME_REGISTER_COMPONENT_FOLDER( PhysicsJoint, "Physics" )
