#pragma once
#include "ECS/Component.h"
#include "ECS/ComponentDetail.h"
#include "Math/Quaternion.h"
#include "Math/Vector2.h"
#include "Math/Vector3.h"
#include <cstdint>

// A capsule character moved by Box3D's mover: slides along walls, walks up slopes up to
// SlopeLimit, sticks to the ground over small drops, jumps, falls with gravity and pushes dynamic
// bodies. The Transform sits at the capsule's centre plus Center. Steer with SetMoveInput (an
// acceleration-limited desired direction) and/or Move (an explicit displacement this frame).
class CharacterController
    : public Component<CharacterController>
{
    ME_REFLECTABLE( CharacterController )
    friend class PhysicsCore;
public:
    CharacterController();

    Vector3 Center = Vector3( 0.f, 0.f, 0.f );
    float Radius = 0.4f;
    float Height = 1.8f;            // end to end, including the caps
    float MaxSpeed = 6.f;           // m/s for SetMoveInput
    float Acceleration = 40.f;      // m/s^2 towards the desired velocity
    float AirControl = 0.3f;        // fraction of Acceleration while airborne
    float JumpHeight = 1.2f;        // m
    float GravityScale = 1.f;
    float SlopeLimit = 50.f;        // degrees; steeper surfaces are walls
    float GroundSnap = 0.25f;       // m; follow the ground down steps and slopes
    float PushStrength = 1.f;       // impulse scale on dynamic bodies it walks into

    // Desired horizontal direction (world XZ), length <= 1 scales the speed. Kept until changed.
    void SetMoveInput( const Vector3& InDirection );
    // Displacement to add on the next step (accumulates within a frame).
    void Move( const Vector3& InDisplacement );
    void Jump();
    void Teleport( const Vector3& InPosition, const Quaternion& InRotation );

    Vector3 GetPosition() const;
    Vector3 GetVelocity() const { return m_velocity; }
    bool IsOnGround() const { return m_grounded; }
    Vector3 GetGroundNormal() const { return m_groundNormal; }

    // Older gameplay code passed per-frame displacements.
    void Walk( const Vector3& InDisplacement ) { Move( InDisplacement ); }
    void Walk( Vector2 InDirection ) { SetMoveInput( Vector3( InDirection.x, 0.f, InDirection.y ) ); }

private:
    uint64_t m_body = 0;            // kinematic capsule so rigidbodies collide with the character
    uint64_t m_bodySignature = 0;
    Vector3 m_position;             // capsule centre after the last step
    Vector3 m_previousPosition;     // ... and the step before (interpolation)
    Vector3 m_appliedCenter;        // capsule centre last written to the Transform
    bool m_hasPosition = false;
    Vector3 m_velocity;
    Vector3 m_moveInput;
    Vector3 m_pendingMove;
    bool m_wantsJump = false;
    bool m_grounded = false;
    Vector3 m_groundNormal = Vector3( 0.f, 1.f, 0.f );

    void OnDeserialize( const json& InJson ) override;
};
ME_REGISTER_COMPONENT_FOLDER( CharacterController, "Physics" )
