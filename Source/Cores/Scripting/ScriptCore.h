#pragma once

#include "ECS/Core.h"
#include "Scripting/MonoFwd.h"
#include "Path.h"
#include "Events/EventReceiver.h"

class ScriptCore
    : public Core<ScriptCore>
    , public EventReceiver
{
public:

    ScriptCore();
    ~ScriptCore();

    // Separate init from construction code.
    void Init() final;

    // Each core must update each loop
    void Update( const UpdateContext& inUpdateContext ) final;
    void LateUpdate( const UpdateContext& inUpdateContext ) final;

    void OnEntityAdded( Entity& NewEntity ) final;
    void OnEntityRemoved( Entity& InEntity ) final;

#if USING( ME_EDITOR )
    void OnEditorInspect() final;
#endif

    bool OnEvent( const BaseEvent& evt ) override;

private:

};

ME_REGISTER_CORE( ScriptCore )