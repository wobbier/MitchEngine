#pragma once
#include "ECS/Component.h"
#include "ECS/ComponentDetail.h"
#include "Navigation/NavigationTypes.h"
#include "Math/Vector3.h"
#include <cstdint>

// Changes how an entity's geometry is baked (and, with ApplyToChildren, its descendants'): leave it
// out, or give its surfaces another area (e.g. Not Walkable, or a costly "Water" area).
class NavMeshModifier
    : public Component<NavMeshModifier>
{
    ME_REFLECTABLE( NavMeshModifier )
public:
    NavMeshModifier();

    bool IgnoreFromBuild = false;
    bool OverrideArea = true;
    int Area = NavAreas::NotWalkable;
    bool ApplyToChildren = true;

private:
    void OnDeserialize( const json& InJson ) override;
};
ME_REGISTER_COMPONENT_FOLDER( NavMeshModifier, "Navigation" )

// Re-marks the navmesh inside a box (rotates with the entity about Y): carve a hole with Not
// Walkable, or mark a region with a costlier area.
class NavMeshModifierVolume
    : public Component<NavMeshModifierVolume>
{
    ME_REFLECTABLE( NavMeshModifierVolume )
public:
    NavMeshModifierVolume();

    Vector3 Center = Vector3( 0.f, 0.f, 0.f );
    Vector3 Size = Vector3( 2.f, 2.f, 2.f );
    int Area = NavAreas::NotWalkable;

private:
    void OnDeserialize( const json& InJson ) override;
};
ME_REGISTER_COMPONENT_FOLDER( NavMeshModifierVolume, "Navigation" )

// An off-mesh connection between two points (local to the entity): agents path across gaps, down
// ledges or up walls through it, with the cost of its area (Jump by default).
class NavMeshLink
    : public Component<NavMeshLink>
{
    ME_REFLECTABLE( NavMeshLink )
public:
    NavMeshLink();

    Vector3 StartPoint = Vector3( 0.f, 0.f, -1.f );
    Vector3 EndPoint = Vector3( 0.f, 0.f, 1.f );
    float Radius = 0.5f;            // how close to each end the navmesh must be
    bool Bidirectional = true;
    int Area = NavAreas::Jump;

private:
    void OnDeserialize( const json& InJson ) override;
};
ME_REGISTER_COMPONENT_FOLDER( NavMeshLink, "Navigation" )

enum class NavObstacleShape : uint8_t
{
    Box = 0,
    Cylinder,   // upright; Size.x is the diameter, Size.y the height
};

// Something agents must walk around that moves at runtime (a door, a crate, a parked car). While
// the game runs, its footprint (grown by the agent radius) is carved out of the navmesh: when it
// has moved more than MoveThreshold and then stood still for CarveDelay, the tiles under its old and
// new place are rebuilt in the background. Obstacles are left out of bakes.
class NavMeshObstacle
    : public Component<NavMeshObstacle>
{
    ME_REFLECTABLE( NavMeshObstacle )
public:
    NavMeshObstacle();

    NavObstacleShape Shape = NavObstacleShape::Box;
    Vector3 Center = Vector3( 0.f, 0.f, 0.f );
    Vector3 Size = Vector3( 1.f, 1.f, 1.f );
    float MoveThreshold = 0.1f;
    float CarveDelay = 0.25f;

private:
    void OnDeserialize( const json& InJson ) override;
};
ME_REGISTER_COMPONENT_FOLDER( NavMeshObstacle, "Navigation" )

namespace NavigationUI
{
    // Choice names for reflected area / mask fields.
    std::string AreaName( int InArea );
    std::string LayerName( int InLayer );
}
