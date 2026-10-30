#include "PCH.h"
#include "Net.bindings.h"

#if USING( ME_SCRIPTING )

#include "Cores/NetCore.h"
#include "Cores/PhysicsCore.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Scripting/Bindings/BindingContext.h"
#include <algorithm>

using ScriptBindings::MakeHandle;

// C#'s Net (NET-6): the session of the script world's NetCore, commands, ownership and RPCs.
namespace
{
    NetCore* Net()
    {
        SharedPtr<World> world = ScriptBindings::GetWorld().lock();
        NetCore* net = world ? world->GetCore<NetCore>() : nullptr;
        return net && net->IsActive() ? net : nullptr;
    }

    // The script world's NetCore with or without a session (offline play raycasts the present).
    NetCore* AnyNet()
    {
        SharedPtr<World> world = ScriptBindings::GetWorld().lock();
        return world ? world->GetCore<NetCore>() : nullptr;
    }
}


static int Eng_Net_GetRole() noexcept
{
    NetCore* net = Net();
    return !net ? 0 : net->IsServer() ? 1 : 2;
}


static int Eng_Net_GetLocalClientId() noexcept
{
    NetCore* net = Net();
    return net && net->GetClient() && net->GetClient()->GetState() == NetClient::State::Connected ? net->GetClient()->GetClientId() : 0;
}


static int Eng_Net_GetTick() noexcept
{
    NetCore* net = Net();
    return net ? static_cast<int>( net->GetTick() ) : 0;
}


static bool Eng_Net_IsResimulating() noexcept
{
    NetCore* net = Net();
    return net && net->IsResimulating();
}


static int Eng_Net_GetClients( int* outIds, int max ) noexcept
{
    NetCore* net = Net();
    if( !net || !net->GetServer() )
    {
        return 0;
    }
    int count = 0;
    for( const auto& client : net->GetServer()->GetClients() )
    {
        if( client->Accepted )
        {
            if( outIds && count < max )
            {
                outIds[count] = client->Id;
            }
            ++count;
        }
    }
    return count;
}


static int Eng_Net_GetOwner( EntityID id ) noexcept
{
    NetCore* net = Net();
    EntityHandle entity = MakeHandle( id );
    return net && entity ? net->GetOwner( *entity.Get() ) : 0;
}


static void Eng_Net_SetOwner( EntityID id, int clientId ) noexcept
{
    NetCore* net = Net();
    EntityHandle entity = MakeHandle( id );
    if( net && entity && clientId >= 0 && clientId <= 255 )
    {
        net->SetOwner( *entity.Get(), static_cast<uint8_t>( clientId ) );
    }
}


static bool Eng_Net_IsPredicted( EntityID id ) noexcept
{
    NetCore* net = Net();
    EntityHandle entity = MakeHandle( id );
    return net && entity && net->IsPredicted( *entity.Get() );
}


static bool Eng_Net_GetCommand( EntityID id, int* outButtons, int* outPreviousButtons, float* outAxes, int* outTick ) noexcept
{
    NetCore* net = Net();
    EntityHandle entity = MakeHandle( id );
    InputCommand command;
    InputCommand previous;
    const bool found = net && entity && net->GetCommand( *entity.Get(), command, previous );
    if( outButtons )
    {
        *outButtons = static_cast<int>( command.Buttons );
    }
    if( outPreviousButtons )
    {
        *outPreviousButtons = static_cast<int>( previous.Buttons );
    }
    if( outAxes )
    {
        std::copy( command.Axes.begin(), command.Axes.end(), outAxes );
    }
    if( outTick )
    {
        *outTick = static_cast<int>( command.Tick );
    }
    return found;
}


static int Eng_Net_GetActionIndex( const uint8_t* action ) noexcept
{
    return action ? GetEngine().GetNetLaunch().GetActionIndex( reinterpret_cast<const char*>( action ) ) : -1;
}


static void Eng_Net_SetActionOverride( int index, bool held ) noexcept
{
    GetEngine().GetNetLaunch().SetActionOverride( index, held );
}


static void Eng_Net_SendRpc( EntityID id, int rpcId, int kind, uint8_t* data, int size ) noexcept
{
    NetCore* net = Net();
    EntityHandle entity = MakeHandle( id );
    if( net && entity && size >= 0 && kind >= 0 && kind <= 2 )
    {
        net->SendRpc( *entity.Get(), rpcId, static_cast<NetCore::RpcKind>( kind ), data, static_cast<size_t>( size ) );
    }
}


static int Eng_Net_GetNetId( EntityID id ) noexcept
{
    NetCore* net = Net();
    EntityHandle entity = MakeHandle( id );
    return net && entity ? static_cast<int>( net->GetNetId( *entity.Get() ) ) : 0;
}


// Through an out parameter: EntityID returned by value isn't a plain struct on every ABI (MSVC x64).
static void Eng_Net_FindEntity( int netId, EntityID* outId ) noexcept
{
    if( !outId )
    {
        return;
    }
    NetCore* net = Net();
    EntityHandle entity = net && netId > 0 ? net->FindEntity( static_cast<uint32_t>( netId ) ) : EntityHandle();
    *outId = entity ? entity->GetId() : EntityID();
}


static void Eng_Net_GetPredictionStats( int* outCorrections, int* outMatches, int* outInitialSyncs ) noexcept
{
    NetCore* net = Net();
    const NetPredictor* predictor = net ? net->GetPredictor() : nullptr;
    if( outCorrections )
    {
        *outCorrections = predictor ? predictor->GetCorrectionCount() : 0;
    }
    if( outMatches )
    {
        *outMatches = predictor ? predictor->GetMatchCount() : 0;
    }
    if( outInitialSyncs )
    {
        *outInitialSyncs = predictor ? predictor->GetInitialSyncCount() : 0;
    }
}


// NET-15: the server rewinds to the view the shooter's command was sent from (a shooter without
// one is judged now); a client, or offline play, tests the hitboxes as they are now (for FX).
static bool Eng_Net_LagCompensatedRaycast( EntityID shooter, const Vector3* origin, const Vector3* direction, float range, EntityID* outEntity, Vector3* outPoint, int* outFlags ) noexcept
{
    NetCore* net = AnyNet();
    EntityHandle shooterHandle = MakeHandle( shooter );
    LagCompensatedHit hit;
    bool rewound = false;
    if( net && origin && direction )
    {
        InputCommand command;
        InputCommand previous;
        if( net->IsActive() && net->IsServer() && shooterHandle && net->GetCommand( *shooterHandle.Get(), command, previous ) )
        {
            hit = net->LagCompensatedRaycast( *origin, *direction, range, command, shooterHandle );
            rewound = true;
        }
        else if( net->IsActive() && net->IsServer() )
        {
            hit = net->GetLagCompensation().Raycast( net->GetWorld().GetCore<PhysicsCore>(), *origin, *direction, range, -1.0, shooterHandle );
        }
        else
        {
            hit = net->PresentRaycast( *origin, *direction, range, shooterHandle );
        }
    }
    if( outEntity )
    {
        *outEntity = hit.Entity ? hit.Entity->GetId() : EntityID();
    }
    if( outPoint )
    {
        *outPoint = hit.Point;
    }
    if( outFlags )
    {
        *outFlags = ( hit.Hit ? 1 : 0 ) | ( hit.Occluded ? 2 : 0 ) | ( hit.Rejected ? 4 : 0 ) | ( hit.Clamped ? 8 : 0 ) | ( rewound ? 16 : 0 );
    }
    return hit.Hit;
}


static double Eng_Net_GetCommandViewTick( EntityID id ) noexcept
{
    NetCore* net = Net();
    EntityHandle entity = MakeHandle( id );
    InputCommand command;
    InputCommand previous;
    return net && entity && net->GetCommand( *entity.Get(), command, previous ) ? command.ViewTick : -1.0;
}


static float Eng_Net_GetRoundTripMs( int clientId ) noexcept
{
    NetCore* net = Net();
    return net && clientId >= 0 && clientId <= 255 ? net->GetRoundTripMs( static_cast<uint8_t>( clientId ) ) : 0.f;
}


static void Eng_Net_SetAim( bool has, float yaw, float pitch ) noexcept
{
    if( has )
    {
        GetEngine().GetNetLaunch().SetAim( yaw, pitch );
    }
    else
    {
        GetEngine().GetNetLaunch().ClearAim();
    }
}


static bool Eng_Net_GetCommandAim( EntityID id, float* outYaw, float* outPitch ) noexcept
{
    NetCore* net = Net();
    EntityHandle entity = MakeHandle( id );
    InputCommand command;
    InputCommand previous;
    const bool found = net && entity && net->GetCommand( *entity.Get(), command, previous ) && command.HasAim;
    if( outYaw )
    {
        *outYaw = found ? command.Yaw : 0.f;
    }
    if( outPitch )
    {
        *outPitch = found ? command.Pitch : 0.f;
    }
    return found;
}


void Register_NetBindings( ScriptEngineAPI& inAPI )
{
    inAPI.Net_GetRole = &Eng_Net_GetRole;
    inAPI.Net_GetLocalClientId = &Eng_Net_GetLocalClientId;
    inAPI.Net_GetTick = &Eng_Net_GetTick;
    inAPI.Net_IsResimulating = &Eng_Net_IsResimulating;
    inAPI.Net_GetClients = &Eng_Net_GetClients;
    inAPI.Net_GetOwner = &Eng_Net_GetOwner;
    inAPI.Net_SetOwner = &Eng_Net_SetOwner;
    inAPI.Net_IsPredicted = &Eng_Net_IsPredicted;
    inAPI.Net_GetCommand = &Eng_Net_GetCommand;
    inAPI.Net_GetActionIndex = &Eng_Net_GetActionIndex;
    inAPI.Net_SetActionOverride = &Eng_Net_SetActionOverride;
    inAPI.Net_SendRpc = &Eng_Net_SendRpc;
    inAPI.Net_GetNetId = &Eng_Net_GetNetId;
    inAPI.Net_FindEntity = &Eng_Net_FindEntity;
    inAPI.Net_GetPredictionStats = &Eng_Net_GetPredictionStats;
    inAPI.Net_LagCompensatedRaycast = &Eng_Net_LagCompensatedRaycast;
    inAPI.Net_GetCommandViewTick = &Eng_Net_GetCommandViewTick;
    inAPI.Net_GetRoundTripMs = &Eng_Net_GetRoundTripMs;
    inAPI.Net_SetAim = &Eng_Net_SetAim;
    inAPI.Net_GetCommandAim = &Eng_Net_GetCommandAim;
}

#endif
