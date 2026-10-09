#pragma once
#include "ECS/Component.h"
#include "ECS/ComponentDetail.h"
#include "Physics/PhysicsTypes.h"
#include "Math/Vector2.h"
#include <cstdint>

// A 2D physics body (Box2D) in the world XY plane. Its shape comes from the 2D collider components
// on this entity and on child entities without their own Rigidbody2D. The body owns the Transform's
// X/Y position and its rotation about world Z; the Transform's Z position and X/Y tilt are kept.
class Rigidbody2D
    : public Component<Rigidbody2D>
{
    ME_REFLECTABLE( Rigidbody2D )
    friend class Physics2DCore;
public:
    Rigidbody2D();

    BodyType Type = BodyType::Dynamic;
    float Mass = 1.f;                   // kg; 0 = derive from the colliders' densities
    float LinearDamping = 0.f;
    float AngularDamping = 0.05f;
    float GravityScale = 1.f;
    bool ContinuousCollision = false;   // treat as a bullet against other dynamic bodies (costly)
    bool Interpolate = true;
    bool CanSleep = true;
    bool FreezeRotation = false;

    // Runtime (no-ops until the 2D physics core has created the body)
    bool HasBody() const;
    void AddForce( const Vector2& InForce, ForceMode InMode = ForceMode::Force );
    void AddForceAtPosition( const Vector2& InForce, const Vector2& InWorldPosition, ForceMode InMode = ForceMode::Force );
    void AddTorque( float InTorque, ForceMode InMode = ForceMode::Force );
    Vector2 GetVelocity() const;
    void SetVelocity( const Vector2& InVelocity );
    float GetAngularVelocity() const;   // radians per second
    void SetAngularVelocity( float InRadiansPerSecond );
    // Kinematic bodies: move to a pose over the next fixed step. Angle in degrees.
    void MoveTo( const Vector2& InPosition, float InAngleDegrees );
    // Instantly places the body, keeping its velocity. Angle in degrees.
    void Teleport( const Vector2& InPosition, float InAngleDegrees );
    float GetBodyMass() const;
    bool IsSleeping() const;
    void WakeUp();

private:
    uint64_t m_body = 0;            // b2BodyId (see Physics/Box2DUtils.h)
    bool m_hasPendingMove = false;
    Vector2 m_movePosition;
    float m_moveAngle = 0.f;        // radians

    void OnDeserialize( const json& InJson ) override;
};
ME_REGISTER_COMPONENT_FOLDER( Rigidbody2D, "Physics 2D" )
