#pragma once
#include "ECS/Component.h"
#include "ECS/ComponentDetail.h"
#include "Math/Vector2.h"
#include <cstdint>
#include <vector>

// Settings every 2D collider shares. Colliders attach to the Rigidbody2D on their entity or on the
// nearest ancestor (compound bodies); without one they form a static body. Shapes live in the
// entity's local XY plane and scale with its Transform's X/Y scale.
struct Collider2DSettings
{
    Vector2 Offset = Vector2( 0.f, 0.f );
    bool IsTrigger = false;     // reports overlaps (CollisionEvent with IsTrigger, Is2D) without colliding
    float Friction = 0.6f;
    float Restitution = 0.f;    // bounciness
    float Density = 1.f;        // used when the Rigidbody2D's Mass is 0

    uint64_t SettingsHash() const;

protected:
    // Fits the shape to the entity's mesh the first time a collider is added (not when loaded).
    bool m_isConfigured = false;
};

class BoxCollider2D
    : public Component<BoxCollider2D>
    , public Collider2DSettings
{
    ME_REFLECTABLE( BoxCollider2D )
public:
    BoxCollider2D();
    void Init() override;

    Vector2 Size = Vector2( 1.f, 1.f );
    float EdgeRadius = 0.f;     // rounds the corners (grows the box)

private:
    void OnDeserialize( const json& InJson ) override;
};

class CircleCollider2D
    : public Component<CircleCollider2D>
    , public Collider2DSettings
{
    ME_REFLECTABLE( CircleCollider2D )
public:
    CircleCollider2D();
    void Init() override;

    float Radius = 0.5f;

private:
    void OnDeserialize( const json& InJson ) override;
};

enum class CapsuleDirection2D : uint8_t
{
    Vertical = 0,
    Horizontal,
};

class CapsuleCollider2D
    : public Component<CapsuleCollider2D>
    , public Collider2DSettings
{
    ME_REFLECTABLE( CapsuleCollider2D )
public:
    CapsuleCollider2D();
    void Init() override;

    float Radius = 0.5f;
    float Height = 2.f;         // end to end, including the caps
    CapsuleDirection2D Direction = CapsuleDirection2D::Vertical;

private:
    void OnDeserialize( const json& InJson ) override;
};

// A convex polygon from Points (local space). Box2D polygons have at most 8 vertices: more points
// (or a concave outline) collide as their convex hull reduced to 8 vertices.
class PolygonCollider2D
    : public Component<PolygonCollider2D>
    , public Collider2DSettings
{
    ME_REFLECTABLE( PolygonCollider2D )
public:
    PolygonCollider2D();

    std::vector<Vector2> Points = { Vector2( -0.5f, -0.5f ), Vector2( 0.5f, -0.5f ), Vector2( 0.f, 0.5f ) };
    float EdgeRadius = 0.f;

private:
    void OnDeserialize( const json& InJson ) override;
};

// A polyline of two-sided line segments for level geometry (terrain, platforms, walls). Segments
// have no area: they only collide on static / kinematic bodies.
class EdgeCollider2D
    : public Component<EdgeCollider2D>
    , public Collider2DSettings
{
    ME_REFLECTABLE( EdgeCollider2D )
public:
    EdgeCollider2D();

    std::vector<Vector2> Points = { Vector2( -1.f, 0.f ), Vector2( 1.f, 0.f ) };
    bool Loop = false;          // join the last point back to the first

private:
    void OnDeserialize( const json& InJson ) override;
};

ME_REGISTER_COMPONENT_FOLDER( BoxCollider2D, "Physics 2D" )
ME_REGISTER_COMPONENT_FOLDER( CircleCollider2D, "Physics 2D" )
ME_REGISTER_COMPONENT_FOLDER( CapsuleCollider2D, "Physics 2D" )
ME_REGISTER_COMPONENT_FOLDER( PolygonCollider2D, "Physics 2D" )
ME_REGISTER_COMPONENT_FOLDER( EdgeCollider2D, "Physics 2D" )

namespace Collider2DUtils
{
    // Convex hull of the points (counter-clockwise), reduced to at most InMaxVertices by repeatedly
    // dropping the vertex that removes the least area.
    std::vector<Vector2> ConvexHull( const std::vector<Vector2>& InPoints, size_t InMaxVertices );
}
