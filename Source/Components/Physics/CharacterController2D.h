#pragma once
#include "ECS/Component.h"
#include "ECS/ComponentDetail.h"
#include "Math/Vector2.h"
#include <cstdint>

// A platformer character in the XY plane, moved by Box2D's mover: an upright capsule that runs
// along slopes up to SlopeLimit, sticks to the ground over small drops, jumps (with a short
// coyote time), falls with gravity and pushes dynamic bodies. The Transform sits at the capsule
// centre plus Offset; its Z position and rotation are left alone.
class CharacterController2D
    : public Component<CharacterController2D>
{
    ME_REFLECTABLE( CharacterController2D )
    friend class Physics2DCore;
public:
    CharacterController2D();

    Vector2 Offset = Vector2( 0.f, 0.f );
    float Radius = 0.3f;
    float Height = 1.6f;            // end to end, including the caps
    float MaxSpeed = 7.f;           // m/s for SetMoveInput
    float Acceleration = 60.f;      // m/s^2 towards the desired velocity
    float AirControl = 0.5f;        // fraction of Acceleration while airborne
    float JumpHeight = 2.f;         // m
    float GravityScale = 1.f;
    float SlopeLimit = 50.f;        // degrees; steeper surfaces are walls
    float GroundSnap = 0.2f;        // m; follow the ground down steps and slopes
    float CoyoteTime = 0.1f;        // s; a jump still works this long after walking off a ledge
    float PushStrength = 1.f;       // impulse scale on dynamic bodies it runs into

    // Desired horizontal direction in [-1, 1]. Kept until changed.
    void SetMoveInput( float InHorizontal );
    // Displacement to add on the next step (accumulates within a frame).
    void Move( const Vector2& InDisplacement );
    void Jump();
    void Teleport( const Vector2& InPosition );

    Vector2 GetPosition() const;
    Vector2 GetVelocity() const { return m_velocity; }
    bool IsOnGround() const { return m_grounded; }
    Vector2 GetGroundNormal() const { return m_groundNormal; }

private:
    uint64_t m_body = 0;            // kinematic capsule so rigidbodies collide with the character
    uint64_t m_bodySignature = 0;
    uint64_t m_selfBit = 0;         // category bit that keeps the mover from hitting its own capsule
    Vector2 m_position;             // capsule centre after the last step
    Vector2 m_previousPosition;     // ... and the step before (interpolation)
    Vector2 m_appliedCenter;        // capsule centre last written to the Transform
    bool m_hasPosition = false;
    Vector2 m_velocity;
    float m_moveInput = 0.f;
    Vector2 m_pendingMove;
    bool m_wantsJump = false;
    bool m_grounded = false;
    float m_airTime = 0.f;
    Vector2 m_groundNormal = Vector2( 0.f, 1.f );

    void OnDeserialize( const json& InJson ) override;
};
ME_REGISTER_COMPONENT_FOLDER( CharacterController2D, "Physics 2D" )
