#include "PCH.h"
#include "Scripting/ScriptNet.h"

#if USING( ME_SCRIPTING )

#include "Components/Scripting/ScriptComponent.h"
#include "Engine/Engine.h"
#include "Scripting/ScriptEngine.h"

namespace
{
    // The entity's script handle when its class has networked state with all of InFlags.
    int NetHandle( Entity& InEntity, uint32_t InFlags )
    {
        const ScriptComponent* script = InEntity.TryGetComponent<ScriptComponent>();
        const int handle = script ? script->GetHandle() : -1;
        if( handle < 0 )
        {
            return -1;
        }
        const uint32_t flags = ScriptEngine::NetGetFlags( handle );
        return ( flags & ScriptEngine::NetScript ) && ( flags & InFlags ) == InFlags ? handle : -1;
    }
}


NetCore::ScriptGlue MakeScriptNetGlue()
{
    NetCore::ScriptGlue glue;
    glue.State.WriteReplicated = []( Entity& InEntity, std::vector<uint8_t>& OutBytes ) {
        ScriptEngine::NetWriteReplicated( NetHandle( InEntity, ScriptEngine::NetReplicated ), OutBytes );
    };
    glue.State.ReadReplicated = []( Entity& InEntity, const std::vector<uint8_t>& InBytes, bool InOwned ) {
        ScriptEngine::NetReadReplicated( NetHandle( InEntity, ScriptEngine::NetReplicated ), InBytes, InOwned );
    };
    glue.State.SavePredicted = []( Entity& InEntity, std::vector<uint8_t>& OutBytes ) {
        ScriptEngine::NetSavePredicted( NetHandle( InEntity, ScriptEngine::NetPredicted ), OutBytes );
    };
    glue.RestorePredicted = []( Entity& InEntity, const std::vector<uint8_t>& InBytes ) {
        ScriptEngine::NetRestorePredicted( NetHandle( InEntity, ScriptEngine::NetPredicted ), InBytes );
    };
    // Whatever the pawn's script does in a live tick (it may steer the mover from Net.Command
    // without any [Predicted] state of its own); only Save / Restore need [Predicted].
    glue.SimulatePredicted = []( Entity& InEntity ) {
        const ScriptComponent* script = InEntity.TryGetComponent<ScriptComponent>();
        const int handle = script && script->IsEnabled() ? script->GetHandle() : -1;
        if( handle >= 0 && ( ScriptEngine::GetSchedule( handle ).Callbacks & ScriptEngine::FixedUpdateCallback ) )
        {
            ScriptEngine::ScriptOnFixedUpdate( handle, GetEngine().GetFixedTimeStep() );
        }
    };
    glue.DispatchRpc = []( Entity& InEntity, int InRpcId, const uint8_t* InData, size_t InSize ) {
        ScriptEngine::NetDispatchRpc( NetHandle( InEntity, ScriptEngine::NetRpcs ), InRpcId, InData, InSize );
    };
    glue.SchemaHash = []() { return ScriptEngine::NetSchemaHash(); };
    return glue;
}

#endif
