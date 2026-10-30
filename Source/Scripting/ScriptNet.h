#pragma once
#include "Cores/NetCore.h"

#if USING( ME_SCRIPTING )
// NET-6: NetCore's glue to C# scripts (an entity's ScriptComponent): their generated [Replicated]
// and [Predicted] state, one fixed step of a predicted pawn's scripts in resimulation, RPC
// delivery, and the schema hash. NetLaunch installs it before Host / Connect.
NetCore::ScriptGlue MakeScriptNetGlue();
#endif
