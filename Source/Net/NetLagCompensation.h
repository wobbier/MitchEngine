#pragma once
#include "ECS/EntityHandle.h"
#include "Math/Quaternion.h"
#include "Math/Vector3.h"
#include <cstdint>
#include <deque>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class NetHitbox;
class PhysicsCore;
class World;

struct LagCompensatedHit
{
    bool Hit = false;
    bool Rejected = false;      // the shooter claimed to see further back than the cap
    EntityHandle Entity;        // the hitbox's entity
    Vector3 Point;
    float Distance = 0.f;
    bool Occluded = false;      // a collider was in the way (Entity is still the hitbox it hid)
    double RewoundTicks = 0.0;
    int Tested = 0;             // hitboxes rewound and tested against the ray (after the broadphase)
};


// Lag compensation (NET-8). The server records every NetHitbox's world pose after each tick,
// for MaxRewindSeconds. A shot names the (fractional) server tick its shooter was looking at:
// the render tick of its interpolated view, sent with its command (InputCommand::ViewTick). The
// server tests the ray against the hitboxes as they were then (poses interpolated between
// recorded ticks), analytically: ray vs capsule / oriented box, nothing moved in the physics
// world. Colliders in the present still block it (a raycast for occluders, ignoring hitbox
// owners). A view older than the cap is refused, so a lagging (or lying) client can't shoot
// into the far past.
class NetLagCompensation
{
public:
    // Covers about 300 ms of round trip plus the view's interpolation delay and command slack.
    double MaxRewindSeconds = 0.4;
    bool DrawOverlays = false;      // each shot draws the rewound hitboxes, the present ones and the ray
    // NET-15: cull by each hitbox's bounds swept over the rewind window before rewinding it (the
    // results are the same either way; off only to compare).
    bool UseBroadphase = true;

    // After a server tick: where every hitbox is.
    void Record( World& InWorld, uint32_t InTick );
    void SetTickRate( uint16_t InTickRate ) { m_tickRate = InTickRate; }

    // InViewTick < 0: no rewind (the present). InIgnore: the shooter.
    LagCompensatedHit Raycast( PhysicsCore* InPhysics, const Vector3& InOrigin, const Vector3& InDirection, float InMaxDistance, double InViewTick,
        const EntityHandle& InIgnore = EntityHandle() ) const;

    struct HitboxPose
    {
        EntityHandle Entity;
        uint8_t Shape = 0;
        Vector3 Position;       // the hitbox centre, world
        Quaternion Rotation;
        float Radius = 0.f;
        float HalfHeight = 0.f; // capsule: centre to cap centre
        Vector3 HalfExtents;
    };
    // The hitboxes as they were at InTick (fractional), for overlays and tests.
    std::vector<HitboxPose> GetPosesAt( double InTick ) const;
    uint32_t GetNewestTick() const { return m_frames.empty() ? 0 : m_frames.back().Tick; }

    // Ray against one pose: distance along the (unit) ray, or < 0.
    static float Intersect( const HitboxPose& InPose, const Vector3& InOrigin, const Vector3& InDirection, float InMaxDistance );

private:
    struct Frame
    {
        uint32_t Tick = 0;
        std::vector<HitboxPose> Hitboxes;
    };
    struct Bounds
    {
        Vector3 Min;
        Vector3 Max;
    };
    // The poses at InTick, of only the InOnly entities when given.
    std::vector<HitboxPose> PosesAt( double InTick, const std::unordered_set<uint64_t>* InOnly ) const;
    std::deque<Frame> m_frames;
    // Every recorded hitbox's bounds over the whole window (any view tick a shot may name).
    std::unordered_map<uint64_t, Bounds> m_swept;
    uint16_t m_tickRate = 60;
};
