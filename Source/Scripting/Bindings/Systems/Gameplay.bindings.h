#pragma once
#include "Scripting/Generated/ScriptEngineAPI.generated.h"

// Logging, time, entity lifecycle, world-space transforms, prefabs and scenes, input actions,
// audio, physics and debug draw for scripts.
void Register_GameplayBindings( ScriptEngineAPI& inAPI );
