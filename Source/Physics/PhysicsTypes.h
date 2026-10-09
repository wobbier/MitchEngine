#pragma once
#include "ECS/EntityHandle.h"
#include "Events/Event.h"
#include "Math/Vector3.h"
#include <cstdint>

// Collision layers are the entity's layer (0-31); a mask is a bit set of layers. Which layers
// collide is set in Project Settings (layer collision matrix).
namespace PhysicsLayers
{
    constexpr uint32_t All = 0xFFFFFFFFu;

    constexpr uint32_t Bit( uint8_t InLayer )
    {
        return 1u << ( InLayer & 31u );
    }
}

// How a force passed to Rigidbody::AddForce / AddTorque is applied.
enum class ForceMode : uint8_t
{
    Force = 0,          // continuous, mass dependent (N): applied over the next step
    Impulse,            // instant, mass dependent (N s)
    Acceleration,       // continuous, ignores mass (m/s^2)
    VelocityChange,     // instant, ignores mass (m/s)
};

struct RaycastHit
{
    EntityHandle Entity;        // entity owning the collider that was hit
    Vector3 Position;
    Vector3 Normal;
    float Distance = 0.f;
    float Fraction = 0.f;       // along the cast, 0..1
};

// Fired (on the main thread, after each physics step) when two colliders start or stop touching,
// or a collider enters or leaves a trigger. A is the entity whose collider is the trigger, when one
// of them is.
class CollisionEvent
    : public Event<CollisionEvent>
{
public:
    enum class Phase : uint8_t
    {
        Enter = 0,
        Exit,
    };

    CollisionEvent()
        : Event()
    {
    }

    EntityHandle A;
    EntityHandle B;
    Phase State = Phase::Enter;
    bool IsTrigger = false;
    bool Is2D = false;
    Vector3 Point;              // contact point (Enter, non-trigger 3D contacts)
    Vector3 Normal;             // from A towards B
};
