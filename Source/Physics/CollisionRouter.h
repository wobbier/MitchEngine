#pragma once
#include "ECS/EntityHandle.h"
#include "Events/EventReceiver.h"
#include "Physics/PhysicsTypes.h"

#include <functional>
#include <memory>

class Entity;

// A contact or trigger overlap as one subscribed entity sees it.
struct Collision
{
    EntityHandle Self;          // the subscribed entity
    EntityHandle Collider;      // the collider on Self's side: Self, or a child collider of Self's body
    EntityHandle Other;         // the other side's collider entity (empty if it was destroyed this step)
    CollisionEvent::Phase State = CollisionEvent::Phase::Enter;
    bool IsTrigger = false;
    bool Is2D = false;
    Vector3 Point;              // contact point (Enter, non-trigger contacts)
    Vector3 Normal;             // from Self towards Other (Enter, non-trigger contacts)
};

using CollisionHandler = std::function<void( const Collision& )>;

// Keeps a CollisionRouter::Subscribe callback registered; destroying it unsubscribes. Safe to
// destroy from inside the callback, and after the router is gone.
class CollisionSubscription
{
public:
    ~CollisionSubscription();
    CollisionSubscription( const CollisionSubscription& ) = delete;
    CollisionSubscription& operator=( const CollisionSubscription& ) = delete;

private:
    friend class CollisionRouter;
    struct Registry;
    CollisionSubscription( const std::shared_ptr<Registry>& InRegistry, uint64_t InEntityKey, uint64_t InId );

    std::weak_ptr<Registry> m_registry;
    uint64_t m_entityKey = 0;
    uint64_t m_id = 0;
};

// Per-entity collision callbacks for C++ (scripts get OnCollisionEnter & co. from ScriptCore).
// Listens to CollisionEvent (3D and 2D) and calls each side's subscribers once per contact change:
// the collider's entity, and the body that owns it when that's an ancestor (compound bodies).
// Callbacks run on the main thread right after the physics step, before deferred destroys flush,
// so every handle in a Collision is still alive (and handlers may destroy entities). Subscribing
// or unsubscribing from a callback is safe; new subscriptions hear the next contact change.
// The engine owns one (Engine::Collisions); tests may make their own.
//   m_hits = GetEngine().Collisions->Subscribe( entity, [this]( const Collision& hit ) { ... } );
class CollisionRouter
    : public EventReceiver
{
public:
    CollisionRouter();

    // Null if InEntity is gone or InHandler is empty.
    [[nodiscard]] std::unique_ptr<CollisionSubscription> Subscribe( const EntityHandle& InEntity, CollisionHandler InHandler );

    // The entities that hear about a contact on InCollider's collider: InCollider itself and, when it
    // has no Rigidbody / Rigidbody2D, the nearest ancestor that has one. Returns how many (1 or 2).
    static int ResolveTargets( Entity& InCollider, Entity* OutTargets[2] );

private:
    void OnCollision( const CollisionEvent& InEvent );
    void Deliver( const EntityHandle& InSelf, const EntityHandle& InOther, const CollisionEvent& InEvent, const Vector3& InNormal );

    std::shared_ptr<CollisionSubscription::Registry> m_registry;
};
