#pragma once

#include "ECS/Core.h"
#include "ECS/CoreDetail.h"
#include "Events/EventReceiver.h"

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
    void FixedUpdate( const UpdateContext& inUpdateContext ) final;
    void Update( const UpdateContext& inUpdateContext ) final;
    void LateUpdate( const UpdateContext& inUpdateContext ) final;

    void OnEntityAdded( Entity& NewEntity ) final;
    void OnEntityRemoved( Entity& InEntity ) final;
    bool OnEvent( const BaseEvent& InEvent ) final;

#if USING( ME_EDITOR )
    void OnEditorInspect() final;
#endif

private:
    bool m_running = false;
    uint64_t m_nextStartSequence = 0;
    // The scripts running a callback this tick, in update order (RunList).
    std::vector<ScriptComponent*> m_runList;

    void StartScript( ScriptComponent& InScript );
    void StartPendingScripts();
    // Started, enabled and active scripts whose class implements InCallback, by [ExecutionOrder]
    // then start order.
    const std::vector<ScriptComponent*>& RunList( uint32_t InCallback );
    static void RefreshSchedule( ScriptComponent& InScript );
    static bool RunsBefore( const ScriptComponent* InA, const ScriptComponent* InB );
    void DeliverCollision( const EntityHandle& InSelf, const EntityHandle& InOther, int InKind, const Vector3& InPoint, const Vector3& InNormal );
};

ME_REGISTER_CORE( ScriptCore )
