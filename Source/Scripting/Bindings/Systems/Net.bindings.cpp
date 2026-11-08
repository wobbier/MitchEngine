#include "PCH.h"
#include "Components/Networking/NetIdentity.h"
#include "Net.bindings.h"

#if USING( ME_SCRIPTING )

#include "Cores/NetCore.h"
#include "Cores/PhysicsCore.h"
#include "Components/Networking/SingletonInput.h"
#include "Components/Networking/SingletonKillcam.h"
#include "Engine/KillcamPlayer.h"
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

    // The script world's sampled input (ME-297): its networked actions' layout, the local command,
    // what scripts hold and aim. Null without a script world.
    SingletonInput* WorldInput()
    {
        SharedPtr<World> world = ScriptBindings::GetWorld().lock();
        return world ? &world->Singleton<SingletonInput>() : nullptr;
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
    SingletonInput* input = WorldInput();
    return action && input ? input->GetActionIndex( reinterpret_cast<const char*>( action ) ) : -1;
}


static void Eng_Net_SetActionOverride( int index, bool held ) noexcept
{
    if( SingletonInput* input = WorldInput() )
    {
        input->SetActionOverride( index, held );
    }
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
static bool Eng_Net_LagCompensatedRaycast( EntityID shooter, EntityID instigator, const Vector3* origin, const Vector3* direction, float range, EntityID* outEntity, Vector3* outPoint, int* outFlags ) noexcept
{
    NetCore* net = AnyNet();
    EntityHandle shooterHandle = MakeHandle( shooter );
    EntityHandle instigatorHandle = MakeHandle( instigator );
    LagCompensatedHit hit;
    bool rewound = false;
    if( net && origin && direction )
    {
        InputCommand command;
        InputCommand previous;
        if( net->IsActive() && net->IsServer() && shooterHandle && net->GetCommand( *shooterHandle.Get(), command, previous ) )
        {
            hit = net->LagCompensatedRaycast( *origin, *direction, range, command, shooterHandle, instigatorHandle );
            rewound = true;
        }
        else if( net->IsActive() && net->IsServer() )
        {
            // No command (a bot): judged at the newest recorded tick, and recorded like any shot (ME-316).
            InputCommand present;
            present.ViewTick = -1.0;
            hit = net->LagCompensatedRaycast( *origin, *direction, range, present, shooterHandle, instigatorHandle );
        }
        else
        {
            hit = net->PresentRaycast( *origin, *direction, range, shooterHandle, instigatorHandle );
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
    SingletonInput* input = WorldInput();
    if( !input )
    {
        return;
    }
    if( has )
    {
        input->SetAim( yaw, pitch );
    }
    else
    {
        input->ClearAim();
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


static int Eng_Net_GetAxisIndex( const uint8_t* action, int* outCount ) noexcept
{
    int count = 0;
    SingletonInput* input = WorldInput();
    const int index = action && input ? input->GetAxisIndex( reinterpret_cast<const char*>( action ), &count ) : -1;
    if( outCount )
    {
        *outCount = count;
    }
    return index;
}


static int Eng_Net_GetLayoutVersion() noexcept
{
    SingletonInput* input = WorldInput();
    return input ? static_cast<int>( input->GetLayoutVersion() ) : 0;
}


// C#'s Net.LocalCommand. Offline: the command the engine sampled for this fixed step. A client:
// its predicted pawn's (a replayed tick's while resimulating), or before it has one, the command
// it sends. The server has no local player. Flags: 1 valid, 2 has an aim.
static int Eng_Net_GetLocalCommand( int* outButtons, int* outPreviousButtons, float* outAxes, float* outAim, double* outViewTick, int* outTick ) noexcept
{
    NetCore* net = Net();
    const SingletonInput* input = WorldInput();
    InputCommand command;
    InputCommand previous;
    bool valid = false;
    if( !net )
    {
        if( input && input->HasLocalCommand() )
        {
            valid = true;
            command = input->GetLocalCommand();
            previous = input->GetPreviousLocalCommand();
            command.ViewTick = -1.0;
        }
    }
    else if( net->IsClient() )
    {
        const NetPredictor* predictor = net->GetPredictor();
        if( predictor && predictor->GetPawn() )
        {
            valid = true;
            command = predictor->GetCommand();
            previous = predictor->GetPreviousCommand();
        }
        else if( input && input->HasLocalCommand() )
        {
            valid = true;
            command = input->GetLocalCommand();
            previous = input->GetPreviousLocalCommand();
            command.Tick = net->GetTick();
        }
    }
    if( !valid )
    {
        command = InputCommand();
        previous = InputCommand();
    }
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
    if( outAim )
    {
        outAim[0] = command.HasAim ? command.Yaw : 0.f;
        outAim[1] = command.HasAim ? command.Pitch : 0.f;
    }
    if( outViewTick )
    {
        *outViewTick = command.ViewTick;
    }
    if( outTick )
    {
        *outTick = static_cast<int>( command.Tick );
    }
    return valid ? ( 1 | ( command.HasAim ? 2 : 0 ) ) : 0;
}


// The predicted pawn's correction still being smoothed out (zero for anything else).
static void Eng_Net_GetVisualOffset( EntityID id, Vector3* outOffset ) noexcept
{
    if( !outOffset )
    {
        return;
    }
    NetCore* net = Net();
    EntityHandle entity = MakeHandle( id );
    const NetPredictor* predictor = net && net->IsClient() ? net->GetPredictor() : nullptr;
    *outOffset = predictor && entity && predictor->GetPawn() == entity ? predictor->GetVisualOffset() : Vector3();
}


// C#'s Replay (ME-316): game events into the server's match replay, or an offline session's
// recording (ME-215: no session, so AnyNet).
static void Eng_Replay_Event( const uint8_t* name, const uint8_t* payload, EntityID id ) noexcept
{
    NetCore* net = AnyNet();
    if( !net || !net->IsRecording() || !name )
    {
        return;
    }
    // The NetId is looked up when the tick is recorded: a pawn instantiated this tick has none yet.
    net->RecordReplayEvent( reinterpret_cast<const char*>( name ), payload ? reinterpret_cast<const char*>( payload ) : "", MakeHandle( id ) );
}


static bool Eng_Replay_IsRecording() noexcept
{
    NetCore* net = AnyNet();
    return net && net->IsRecording();
}


// C#'s Replay.IsPlaying / Replay.Tick (ME-316 phase 2): a replay viewer's playhead.
static bool Eng_Replay_IsPlaying() noexcept
{
    NetCore* net = Net();
    return net && net->IsPlayback();
}


static int Eng_Replay_GetTick() noexcept
{
    NetCore* net = Net();
    return net && net->IsPlayback() ? static_cast<int>( net->GetPlayback()->GetTick() ) : -1;
}


// C#'s Net.SendKillcam / Net.Killcam (ME-323): the server cuts and streams one; a client's newest.
static bool Eng_Net_SendKillcam( EntityID victim, EntityID killer, float seconds ) noexcept
{
    NetCore* net = Net();
    return net && net->IsServer() && net->SendKillcam( MakeHandle( victim ), MakeHandle( killer ), static_cast<double>( seconds ) );
}


// The script world's killcam player (ME-323 phase 3), and whether that world is its replay world.
static KillcamPlayer* KillcamOf( bool* OutInReplayWorld = nullptr )
{
    SharedPtr<World> world = ScriptBindings::GetWorld().lock();
    SingletonKillcam* singleton = world ? world->FindSingleton<SingletonKillcam>() : nullptr;
    if( OutInReplayWorld )
    {
        *OutInReplayWorld = singleton && singleton->InReplayWorld;
    }
    return singleton ? singleton->Player : nullptr;
}


static bool Eng_Net_GetKillcam( int* outId, float* outProgress, EntityID* outKiller, EntityID* outVictim ) noexcept
{
    // In a killcam's replay world: the one it plays, its killer and victim as that world has them.
    bool inReplay = false;
    if( KillcamPlayer* player = KillcamOf( &inReplay ); player && inReplay && player->IsPlaying() )
    {
        const KillcamPlayer::Status& status = player->GetStatus();
        auto here = [player]( uint32_t InNetId ) {
            EntityHandle entity = player->GetNet()->FindEntity( InNetId );
            return entity ? entity->GetId() : EntityID();
        };
        if( outId ) *outId = static_cast<int>( status.Id );
        if( outProgress ) *outProgress = status.Complete ? 1.f : 0.99f;
        if( outKiller ) *outKiller = here( status.KillerNetId );
        if( outVictim ) *outVictim = here( status.VictimNetId );
        return status.Complete;
    }
    NetCore* net = Net();
    const NetKillcamReceiver* killcam = net && net->GetClient() ? &net->GetKillcam() : nullptr;
    if( outId )
    {
        *outId = killcam ? static_cast<int>( killcam->GetId() ) : 0;
    }
    if( outProgress )
    {
        *outProgress = killcam ? net->GetKillcamProgress() : 0.f;
    }
    auto find = [net, killcam]( uint32_t InNetId ) {
        EntityHandle entity = killcam && InNetId != 0 ? net->FindEntity( InNetId ) : EntityHandle();
        return entity ? entity->GetId() : EntityID();
    };
    if( outKiller )
    {
        *outKiller = find( killcam ? killcam->GetKillerNetId() : 0 );
    }
    if( outVictim )
    {
        *outVictim = find( killcam ? killcam->GetVictimNetId() : 0 );
    }
    return killcam && killcam->IsComplete();
}


static int Eng_Net_GetKillcamPlayback( int* outId, int* outKillerId, int* outVictimId, float* outTick, int* outFirstTick, int* outDeathTick, float* outElapsed,
    float* outDuration ) noexcept
{
    bool inReplay = false;
    const KillcamPlayer* player = KillcamOf( &inReplay );
    const KillcamPlayer::Status status = player ? player->GetStatus() : KillcamPlayer::Status();
    if( outId ) *outId = static_cast<int>( status.Id );
    if( outKillerId ) *outKillerId = static_cast<int>( status.KillerNetId );
    if( outVictimId ) *outVictimId = static_cast<int>( status.VictimNetId );
    if( outTick ) *outTick = static_cast<float>( status.Tick );
    if( outFirstTick ) *outFirstTick = static_cast<int>( status.FirstTick );
    if( outDeathTick ) *outDeathTick = static_cast<int>( status.DeathTick );
    if( outElapsed ) *outElapsed = static_cast<float>( status.Elapsed );
    if( outDuration ) *outDuration = static_cast<float>( status.Duration );
    return ( player && player->IsPlaying() ? 1 : 0 ) | ( inReplay ? 2 : 0 );
}


static void Eng_Net_SkipKillcam() noexcept
{
    if( KillcamPlayer* player = KillcamOf() )
    {
        player->Skip();
    }
}


static void Eng_Net_SetKillcamEyeHeight( float height ) noexcept
{
    if( SharedPtr<World> world = ScriptBindings::GetWorld().lock() )
    {
        world->Singleton<SingletonKillcam>().EyeHeight = std::isfinite( height ) && height >= 0.f ? height : -1.f;
    }
}


static float Eng_Net_GetKillcamEyeHeight() noexcept
{
    SharedPtr<World> world = ScriptBindings::GetWorld().lock();
    const SingletonKillcam* singleton = world ? world->FindSingleton<SingletonKillcam>() : nullptr;
    return singleton ? singleton->EyeHeight : -1.f;
}


// ME-343: the script world's session (C#'s Net.SessionId: Net.Events keeps a ledger per session).
static int Eng_Net_GetSessionId() noexcept
{
    NetCore* net = Net();
    return net ? static_cast<int>( net->GetSessionId() ) : 0;
}


void Register_NetBindings( ScriptEngineAPI& inAPI )
{
    // Scripts can add it by name (World.Instantiate + AddComponent("NetIdentity")) and query it.
    ScriptBindings::RegisterComponent<NetIdentity>( "NetIdentity" );
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
    inAPI.Net_GetAxisIndex = &Eng_Net_GetAxisIndex;
    inAPI.Net_GetLayoutVersion = &Eng_Net_GetLayoutVersion;
    inAPI.Net_GetLocalCommand = &Eng_Net_GetLocalCommand;
    inAPI.Net_GetVisualOffset = &Eng_Net_GetVisualOffset;
    inAPI.Replay_Event = &Eng_Replay_Event;
    inAPI.Replay_IsRecording = &Eng_Replay_IsRecording;
    inAPI.Replay_IsPlaying = &Eng_Replay_IsPlaying;
    inAPI.Replay_GetTick = &Eng_Replay_GetTick;
    inAPI.Net_SendKillcam = &Eng_Net_SendKillcam;
    inAPI.Net_GetKillcam = &Eng_Net_GetKillcam;
    inAPI.Net_GetKillcamPlayback = &Eng_Net_GetKillcamPlayback;
    inAPI.Net_SkipKillcam = &Eng_Net_SkipKillcam;
    inAPI.Net_SetKillcamEyeHeight = &Eng_Net_SetKillcamEyeHeight;
    inAPI.Net_GetKillcamEyeHeight = &Eng_Net_GetKillcamEyeHeight;
    inAPI.Net_GetSessionId = &Eng_Net_GetSessionId;
}

#endif
