#pragma once
#include "ECS/Component.h"
#include "ECS/ComponentDetail.h"
#include "Physics/PhysicsTypes.h"
#include "Math/Quaternion.h"
#include "Math/Vector3.h"
#include <cstdint>

// A physics body (Box3D). Its shape comes from the collider components on this entity and on child
// entities without their own Rigidbody (compound bodies). Dynamic bodies drive their Transform;
// kinematic bodies follow it. Entities with colliders but no Rigidbody become static bodies.
class Rigidbody
    : public Component<Rigidbody>
{
    ME_REFLECTABLE( Rigidbody )
    friend class PhysicsCore;
public:
    Rigidbody();

    BodyType Type = BodyType::Dynamic;
    float Mass = 1.f;                   // kg; 0 = derive from the colliders' densities
    float LinearDamping = 0.f;
    float AngularDamping = 0.05f;
    float GravityScale = 1.f;
    bool ContinuousCollision = false;   // sweep fast movers so they can't tunnel (costly)
    bool Interpolate = true;            // smooth rendering between fixed steps
    bool CanSleep = true;
    bool LockPositionX = false;
    bool LockPositionY = false;
    bool LockPositionZ = false;
    bool LockRotationX = false;
    bool LockRotationY = false;
    bool LockRotationZ = false;

    // Runtime (no-ops until the physics core has created the body)
    bool HasBody() const;
    void AddForce( const Vector3& InForce, ForceMode InMode = ForceMode::Force );
    void AddForceAtPosition( const Vector3& InForce, const Vector3& InWorldPosition, ForceMode InMode = ForceMode::Force );
    void AddTorque( const Vector3& InTorque, ForceMode InMode = ForceMode::Force );
    Vector3 GetVelocity() const;
    void SetVelocity( const Vector3& InVelocity );
    Vector3 GetAngularVelocity() const;
    void SetAngularVelocity( const Vector3& InVelocity );
    Vector3 GetPointVelocity( const Vector3& InWorldPoint ) const;
    // Kinematic bodies: move to a pose over the next fixed step (pushes dynamic bodies on the way).
    void MoveTo( const Vector3& InPosition, const Quaternion& InRotation );
    // Instantly places the body (and its Transform), keeping its velocity.
    void Teleport( const Vector3& InPosition, const Quaternion& InRotation );
    float GetBodyMass() const;
    Vector3 GetCenterOfMass() const;
    bool IsSleeping() const;
    void WakeUp();
    void Sleep();

    // Kept for older gameplay code.
    void ApplyForce( const Vector3& InDirection, float InForce ) { AddForce( InDirection * InForce, ForceMode::Impulse ); }
    void SetMass( float InMass );
    const bool IsDynamic() const { return Type == BodyType::Dynamic; }

private:
    uint64_t m_body = 0;            // b3BodyId (see Physics/Box3DUtils.h)
    bool m_hasPendingMove = false;
    Vector3 m_movePosition;
    Quaternion m_moveRotation;

    void OnDeserialize( const json& InJson ) override;
};
ME_REGISTER_COMPONENT_FOLDER( Rigidbody, "Physics" )
