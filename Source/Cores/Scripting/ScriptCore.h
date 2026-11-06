#pragma once

#include "ECS/Core.h"
#include "ECS/CoreDetail.h"
#include "Events/EventReceiver.h"

class CollisionEvent;
class ScriptComponent;

// Runs C# scripts (engine-owned in scripting builds; scenes that still list "ScriptCore" get this
// instance). Scripts are created with their saved fields as soon as their component loads, so the
// inspector shows live values in edit mode, but only run while the world is started: OnStart when
// it starts (scripts spawned during play start before their first update, after physics has
// created their bodies), then OnFixedUpdate / OnUpdate / OnLateUpdate (after animation) each step,
// in [ExecutionOrder] then start order, calling only the callbacks a class implements. Physics
// contacts and trigger overlaps (CollisionEvent, 3D and 2D) reach the scripts on both entities and
// on their bodies.
class ScriptCore final
    : public Core<ScriptCore>
    , public EventReceiver
{
public:
    ScriptCore();

    void OnStart() final;
    void OnStop() final;
    // ED-9, the editor's Simulate: Play without scripts (none start or update until it's cleared
    // and the world starts again); physics, animation and the other cores run.
    void SetSuspended( bool InSuspended ) { m_suspended = InSuspended; }
    bool IsSuspended() const { return m_suspended; }
    // ME-323: a replay world's (a killcam's) script core. Its scripts ([ReplayWorld] ones: no other
    // class gets an instance in a replay world) present only: OnStart, OnUpdate and OnLateUpdate, each
    // inside a WorldScope of its world so their engine calls act on it, and never OnFixedUpdate. It
    // leaves everything process-wide to the game world's ScriptCore: no engine events for C#'s
    // EventBus, no scheduler ticks or resets (awaits, timers and tweens are the game's).
    void SetPresentationOnly( bool InPresentationOnly ) { m_presentationOnly = InPresentationOnly; }
    bool IsPresentationOnly() const { return m_presentationOnly; }
    void FixedUpdate( const UpdateContext& inUpdateContext ) final;
    void Update( const UpdateContext& inUpdateContext ) final;
    void LateUpdate( const UpdateContext& inUpdateContext ) final;

    void OnEntityAdded( Entity& NewEntity ) final;
    void OnEntityRemoved( Entity& InEntity ) final;

#if USING( ME_EDITOR )
    void OnEditorInspect() final;
#endif

private:
    bool m_running = false;
    bool m_suspended = false;
    bool m_presentationOnly = false;
    uint64_t m_nextStartSequence = 0;
    // The scripts running a callback this tick, in update order (RunList).
    std::vector<ScriptComponent*> m_runList;
    // Every script of the core in update order ([ExecutionOrder], then start order), sorted again only
    // when a script joins, leaves or starts, or the scripts reload; RunList filters it per phase.
    std::vector<ScriptComponent*> m_orderedScripts;
    bool m_orderDirty = true;
    uint32_t m_orderGeneration = 0;

    void StartScript( ScriptComponent& InScript );
    void StartPendingScripts();
    // Started, enabled and active scripts whose class implements InCallback, by [ExecutionOrder]
    // then start order.
    const std::vector<ScriptComponent*>& RunList( uint32_t InCallback );
    static void RefreshSchedule( ScriptComponent& InScript );
    static bool RunsBefore( const ScriptComponent* InA, const ScriptComponent* InB );
    void OnCollision( const CollisionEvent& InEvent );
    // CS-8: engine events queued for C#'s EventBus, delivered before the next update's scripts.
    void QueueEngineEvent( json InEvent );
    std::vector<json> m_engineEvents;
    void DeliverCollision( const EntityHandle& InSelf, const EntityHandle& InOther, int InKind, const Vector3& InPoint, const Vector3& InNormal );
};

ME_REGISTER_CORE( ScriptCore )
