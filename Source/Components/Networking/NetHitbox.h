#pragma once
#include "ECS/Component.h"
#include "ECS/ComponentDetail.h"
#include "Math/Vector3.h"

enum class NetHitboxShape : uint8_t
{
    Capsule = 0,    // along the entity's local Y: Height end to end, Radius
    Box,            // HalfExtents
};

// A shape lag-compensated shots test against (NET-8): the server keeps where each one was for the
// last ~250 ms and rewinds to what the shooter saw (NetLagCompensation). Hitboxes aren't
// colliders; bodies still collide as usual. Several on one entity: one per child entity.
class NetHitbox
    : public Component<NetHitbox>
{
    ME_REFLECTABLE( NetHitbox )
public:
    NetHitbox();

    NetHitboxShape Shape = NetHitboxShape::Capsule;
    Vector3 Center;                         // local
    float Radius = 0.4f;
    float Height = 1.8f;
    Vector3 HalfExtents = Vector3( 0.5f, 0.5f, 0.5f );
    // NET-15: a moving blocker (a door, a platform, the payload), not something to hit: a shot is
    // blocked by it where it was in the shooter's view, and this entity's present-day colliders
    // are ignored. Give it a shape covering the collider.
    bool Occluder = false;
};
ME_REGISTER_COMPONENT_FOLDER( NetHitbox, "Networking" )
