#pragma once
#include "ECS/Component.h"
#include "ECS/ComponentDetail.h"
#include "Math/Vector3.h"
#include "Math/Bounds.h"
#include <cstdint>

// Settings every 3D collider shares. Colliders attach to the Rigidbody on their entity or on the
// nearest ancestor (compound bodies); without one they form a static body. Sizes are in the
// entity's local space and scale with its Transform.
struct ColliderSettings
{
    Vector3 Center = Vector3( 0.f, 0.f, 0.f );
    bool IsTrigger = false;     // reports overlaps (CollisionEvent with IsTrigger) without colliding
    float Friction = 0.6f;
    float Restitution = 0.f;    // bounciness
    float Density = 1.f;        // used when the Rigidbody's Mass is 0

    uint64_t SettingsHash() const;

protected:
    // Fits the shape to the entity's mesh the first time a collider is added (not when loaded).
    bool m_isConfigured = false;
};

class BoxCollider
    : public Component<BoxCollider>
    , public ColliderSettings
{
    ME_REFLECTABLE( BoxCollider )
public:
    BoxCollider();
    void Init() override;

    Vector3 Size = Vector3( 1.f, 1.f, 1.f );

private:
    void OnDeserialize( const json& InJson ) override;
};

class SphereCollider
    : public Component<SphereCollider>
    , public ColliderSettings
{
    ME_REFLECTABLE( SphereCollider )
public:
    SphereCollider();
    void Init() override;

    float Radius = 0.5f;

private:
    void OnDeserialize( const json& InJson ) override;
};

enum class CapsuleAxis : uint8_t
{
    X = 0,
    Y,
    Z,
};

class CapsuleCollider
    : public Component<CapsuleCollider>
    , public ColliderSettings
{
    ME_REFLECTABLE( CapsuleCollider )
public:
    CapsuleCollider();
    void Init() override;

    float Radius = 0.5f;
    float Height = 2.f;         // end to end, including the caps
    CapsuleAxis Direction = CapsuleAxis::Y;

private:
    void OnDeserialize( const json& InJson ) override;
};

// Collision geometry from the entity's Mesh: a convex hull (any body type), or the exact triangles
// (static / kinematic bodies only; dynamic bodies don't collide with triangle meshes).
class MeshCollider
    : public Component<MeshCollider>
    , public ColliderSettings
{
    ME_REFLECTABLE( MeshCollider )
public:
    MeshCollider();

    bool Convex = true;

private:
    void OnDeserialize( const json& InJson ) override;
};

ME_REGISTER_COMPONENT_FOLDER( BoxCollider, "Physics" )
ME_REGISTER_COMPONENT_FOLDER( SphereCollider, "Physics" )
ME_REGISTER_COMPONENT_FOLDER( CapsuleCollider, "Physics" )
ME_REGISTER_COMPONENT_FOLDER( MeshCollider, "Physics" )

namespace ColliderUtils
{
    // The local bounds of the entity's mesh, if it has one with geometry.
    bool GetMeshBounds( Entity& InEntity, AABB& OutBounds );
}
