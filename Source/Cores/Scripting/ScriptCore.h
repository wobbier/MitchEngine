#pragma once

#include "ECS/Core.h"
#include "ECS/CoreDetail.h"

class ScriptComponent;

// Runs C# scripts (engine-owned in scripting builds; scenes that still list "ScriptCore" get this
// instance). Scripts are created with their saved fields as soon as their component loads, so the
// inspector shows live values in edit mode, but only run while the world is started: OnStart when
// it starts (scripts spawned during play start before their first update, after physics has
// created their bodies), then OnFixedUpdate / OnUpdate each step.
class ScriptCore final
    : public Core<ScriptCore>
{
public:
    ScriptCore();

    void OnStart() final;
    void OnStop() final;
    void FixedUpdate( const UpdateContext& inUpdateContext ) final;
    void Update( const UpdateContext& inUpdateContext ) final;

    void OnEntityAdded( Entity& NewEntity ) final;
    void OnEntityRemoved( Entity& InEntity ) final;

#if USING( ME_EDITOR )
    void OnEditorInspect() final;
#endif

private:
    bool m_running = false;

    void StartScript( ScriptComponent& InScript );
    void StartPendingScripts();
};

ME_REGISTER_CORE( ScriptCore )
