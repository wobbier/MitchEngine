#include "PCH.h"
#include "Cores/NetCore.h"
#include <atomic>
#include "Mods/Mods.h"
#include "Components/Networking/NetHitbox.h"
#include "Components/Networking/NetIdentity.h"
#include "Components/Transform.h"
#include "Cores/PhysicsCore.h"
#include "Engine/ProjectSettings.h"
#include "Engine/World.h"
#include "Jobs/BackgroundLane.h"
#include "Net/BitStream.h"
#include "CLog.h"
#include "JSON.h"
#include <algorithm>
#include <fstream>
#include <utility>

#if USING( ME_SCRIPTING )
#include "Scripting/ScriptEngine.h"
#endif

#if USING( ME_EDITOR )
#include <imgui.h>
#endif

namespace
{
    // Closing: held-back packets (a conditioned link's latency) go now, the process is ending.
    constexpr double kReleaseEverything = 3600.0;

    json StatsJson( const NetStats& InStats, double InSeconds )
    {
        json out;
        out["RoundTripMs"] = InStats.RoundTripMs;
        out["RoundTripDeviationMs"] = InStats.RoundTripDeviationMs;
        out["PacketLoss"] = InStats.PacketLoss;
        out["PacketsSent"] = InStats.PacketsSent;
        out["PacketsReceived"] = InStats.PacketsReceived;
        out["PacketsLost"] = InStats.PacketsLost;
        out["PacketsRejected"] = InStats.PacketsRejected;
        out["BytesSent"] = InStats.BytesSent;
        out["BytesReceived"] = InStats.BytesReceived;
        out["ReliableResent"] = InStats.ReliableResent;
        out["KbpsOut"] = InSeconds > 0.0 ? static_cast<double>( InStats.BytesSent ) * 8.0 / 1000.0 / InSeconds : 0.0;
        out["KbpsIn"] = InSeconds > 0.0 ? static_cast<double>( InStats.BytesReceived ) * 8.0 / 1000.0 / InSeconds : 0.0;
        // The blob channel (ME-323).
        json blobs;
        blobs["Sent"] = InStats.BlobsSent;
        blobs["Acked"] = InStats.BlobsAcked;
        blobs["Received"] = InStats.BlobsReceived;
        blobs["Cancelled"] = InStats.BlobsCancelled;
        blobs["CancelledByRemote"] = InStats.BlobsCancelledByRemote;
        blobs["Refused"] = InStats.BlobsRefused;
        blobs["SlicesSent"] = InStats.BlobSlicesSent;
        blobs["SlicesResent"] = InStats.BlobSlicesResent;
        blobs["SlicesDuplicate"] = InStats.BlobSlicesDuplicate;
        blobs["SlicesIgnored"] = InStats.BlobSlicesIgnored;
        blobs["BytesSent"] = InStats.BlobBytesSent;
        blobs["BytesReceived"] = InStats.BlobBytesReceived;
        blobs["Packets"] = InStats.BlobPacketsSent;
        out["Blobs"] = blobs;
        return out;
    }

    // An entity's NetId, or its nearest networked ancestor's (a pawn's Head hitbox): 0 for none.
    const NetIdentity* NetworkedSelfOrAncestor( const EntityHandle& InEntity )
    {
        if( !InEntity )
        {
            return nullptr;
        }
        if( const NetIdentity* identity = InEntity->TryGetComponent<NetIdentity>(); identity && identity->GetNetId() != 0 )
        {
            return identity;
        }
        const Transform* transform = InEntity->TryGetComponent<Transform>();
        for( const Transform* parent = transform ? transform->GetParentTransform() : nullptr; parent; parent = parent->GetParentTransform() )
        {
            if( parent->Parent )
            {
                if( const NetIdentity* identity = parent->Parent->TryGetComponent<NetIdentity>(); identity && identity->GetNetId() != 0 )
                {
                    return identity;
                }
            }
        }
        return nullptr;
    }

    bool IsSelfOrUnder( const EntityHandle& InEntity, const EntityHandle& InRoot )
    {
        if( !InEntity || !InRoot )
        {
            return false;
        }
        if( InEntity == InRoot )
        {
            return true;
        }
        const Transform* transform = InEntity->TryGetComponent<Transform>();
        for( const Transform* parent = transform ? transform->GetParentTransform() : nullptr; parent; parent = parent->GetParentTransform() )
        {
            if( parent->Parent == InRoot )
            {
                return true;
            }
        }
        return false;
    }

    NetReplayHitbox ToReplayHitbox( const NetLagCompensation::HitboxPose& InPose )
    {
        NetReplayHitbox hitbox;
        const NetIdentity* owner = NetworkedSelfOrAncestor( InPose.Entity );
        hitbox.NetId = owner ? owner->GetNetId() : 0;
        hitbox.Name = InPose.Entity ? InPose.Entity->GetName() : std::string();
        hitbox.Shape = InPose.Shape;
        hitbox.Position = InPose.Position;
        hitbox.Rotation = InPose.Rotation;
        hitbox.Radius = InPose.Radius;
        hitbox.HalfHeight = InPose.HalfHeight;
        hitbox.HalfExtents = InPose.HalfExtents;
        hitbox.Occluder = InPose.Occluder;
        return hitbox;
    }

    // A shot as the replay records it, with no hitboxes yet.
    NetReplayShot MakeShot( const Vector3& InOrigin, const Vector3& InDirection, float InMaxDistance, double InViewTick, const EntityHandle& InShooter,
        const EntityHandle& InInstigator, const LagCompensatedHit& InHit )
    {
        NetReplayShot shot;
        if( const NetIdentity* shooter = NetworkedSelfOrAncestor( InShooter ) )
        {
            shot.Shooter = shooter->GetNetId();
            shot.Client = shooter->GetOwner();
        }
        // A projectile's sweep: the shot is its instigator's (an unowned arrow would read as a bot's).
        if( InInstigator && !( InInstigator == InShooter ) )
        {
            if( const NetIdentity* instigator = NetworkedSelfOrAncestor( InInstigator ) )
            {
                shot.Instigator = instigator->GetNetId();
                shot.Client = instigator->GetOwner();
            }
        }
        shot.Origin = InOrigin;
        shot.Direction = InDirection;
        shot.Range = InMaxDistance;
        shot.ViewTick = InViewTick;
        shot.RewoundTicks = InHit.RewoundTicks;
        shot.Hit = InHit.Hit;
        shot.Occluded = InHit.Occluded;
        shot.Clamped = InHit.Clamped;
        shot.Rejected = InHit.Rejected;
        if( InHit.Entity )
        {
            const NetIdentity* target = NetworkedSelfOrAncestor( InHit.Entity );
            shot.Target = target ? target->GetNetId() : 0;
            shot.TargetPart = InHit.Entity->GetName();
        }
        shot.Point = InHit.Point;
        shot.Distance = InHit.Distance;
        shot.Tested = static_cast<uint32_t>( std::max( 0, InHit.Tested ) );
        return shot;
    }

    // NET-7: whose hitboxes a debug shot shows: InTarget's (what it hit, or what hid it), else the
    // networked entity the ray passed nearest within NetDebug::kNearMissMetres (0: none). Never the
    // shooter's.
    uint32_t PickShownEntity( const std::vector<NetLagCompensation::HitboxPose>& InPoses, const Vector3& InOrigin, const Vector3& InDirection, float InRange,
        uint32_t InTarget, const EntityHandle& InShooter )
    {
        if( InTarget != 0 )
        {
            return InTarget;
        }
        const float length = InDirection.Length();
        if( length < 1e-9f )
        {
            return 0;
        }
        const Vector3 direction = InDirection * ( 1.f / length );
        uint32_t best = 0;
        float bestGap = NetDebug::kNearMissMetres;
        for( const NetLagCompensation::HitboxPose& pose : InPoses )
        {
            if( pose.Occluder || IsSelfOrUnder( pose.Entity, InShooter ) )
            {
                continue;
            }
            const NetIdentity* owner = NetworkedSelfOrAncestor( pose.Entity );
            if( !owner )
            {
                continue;
            }
            const float along = std::clamp( ( pose.Position - InOrigin ).Dot( direction ), 0.f, InRange );
            const float reach = pose.Shape == static_cast<uint8_t>( NetHitboxShape::Box ) ? pose.HalfExtents.Length() : pose.HalfHeight + pose.Radius;
            const float gap = ( InOrigin + direction * along - pose.Position ).Length() - reach;
            if( gap <= bestGap )
            {
                bestGap = gap;
                best = owner->GetNetId();
            }
        }
        return best;
    }

    // NET-7: the poses of InShown's hitboxes a debug shot carries, at most NetDebug::kMaxHitboxes, the
    // one struck (InStruck: hit, or what hid it) first so a target with more parts never loses it.
    std::vector<const NetLagCompensation::HitboxPose*> ShownPoses( const std::vector<NetLagCompensation::HitboxPose>& InPoses, uint32_t InShown,
        const EntityHandle& InStruck, const EntityHandle& InShooter )
    {
        std::vector<const NetLagCompensation::HitboxPose*> shown;
        if( InShown == 0 )
        {
            return shown;
        }
        for( const NetLagCompensation::HitboxPose& pose : InPoses )
        {
            if( IsSelfOrUnder( pose.Entity, InShooter ) )
            {
                continue;
            }
            const NetIdentity* owner = NetworkedSelfOrAncestor( pose.Entity );
            if( !owner || owner->GetNetId() != InShown )
            {
                continue;
            }
            if( InStruck && pose.Entity == InStruck )
            {
                shown.insert( shown.begin(), &pose );
            }
            else
            {
                shown.push_back( &pose );
            }
        }
        if( shown.size() > NetDebug::kMaxHitboxes )
        {
            shown.resize( NetDebug::kMaxHitboxes );
        }
        return shown;
    }

    std::string Cut( const std::string& InText, size_t InMax )
    {
        if( InText.size() <= InMax )
        {
            return InText;
        }
        // Not inside a UTF-8 sequence.
        size_t end = InMax;
        while( end > 0 && ( static_cast<unsigned char>( InText[end] ) & 0xC0 ) == 0x80 )
        {
            --end;
        }
        return InText.substr( 0, end );
    }

    // A server's client by id, to send to (NetServer::FindClient is const).
    NetServer::Client* FindAccepted( NetServer* InServer, uint8_t InId )
    {
        if( InServer )
        {
            for( const auto& client : InServer->GetClients() )
            {
                if( client->Id == InId && client->Accepted )
                {
                    return client.get();
                }
            }
        }
        return nullptr;
    }

    const char* StateName( NetClient::State InState )
    {
        switch( InState )
        {
        case NetClient::State::Connecting:
            return "connecting";
        case NetClient::State::Connected:
            return "connected";
        case NetClient::State::Rejected:
            return "rejected";
        case NetClient::State::TimedOut:
            return "timed out";
        default:
            return "disconnected";
        }
    }
}


NetCore::NetCore()
    : Base( ComponentFilter().Requires<NetIdentity>() )
{
    SetIsSerializable( false );
}


NetCore::~NetCore() = default;


NetVersion NetCore::MakeVersion()
{
    if( m_versionOverride )
    {
        return *m_versionOverride;
    }
    NetVersion version;
    const ProjectSettings& settings = ProjectSettings::Get();
    version.Build = NetVersion::Hash( settings.ProductName + "\n" + settings.ProductVersion, NetVersion::Hash( kNetProtocolVersion, 0xCBF29CE484222325ull ) );
    // The networked input actions (NET-6): both sides must read commands the same way.
    uint64_t schema = NetVersion::Hash( m_commandSchema, NetVersion::Hash( "" ) );
    if( m_glue.SchemaHash )
    {
        schema = NetVersion::Hash( m_glue.SchemaHash(), schema );
    }
    version.ScriptSchema = schema;
#if USING( ME_SCRIPTING )
    std::vector<std::string> scripts;
    for( int i = 0; i < ScriptEngine::GetScriptCount(); ++i )
    {
        scripts.push_back( ScriptEngine::GetScriptName( i ) );
    }
    std::sort( scripts.begin(), scripts.end() );
    for( const std::string& script : scripts )
    {
        schema = NetVersion::Hash( script + "\n", schema );
    }
    version.ScriptSchema = schema;
#endif
    std::vector<uint64_t> guids;
    // The placed networked entities (their static NetIds come from this order).
    GetWorld().ForEachEntity( [&guids]( Entity& InEntity ) {
        if( InEntity.HasComponent<NetIdentity>() )
        {
            guids.push_back( InEntity.GetGUID() );
        }
    } );
    std::sort( guids.begin(), guids.end() );
    uint64_t scene = NetVersion::Hash( "" );
    for( const uint64_t guid : guids )
    {
        scene = NetVersion::Hash( guid, scene );
    }
    version.Scene = scene;
    // MOD-3: mods that change what's simulated must match ("netRequired" in mod.json).
    version.SetMods( ModManager::Get().GetNetRequired() );
    return version;
}


bool NetCore::Host( INetTransport& InTransport, uint16_t InTickRate )
{
    if( IsActive() )
    {
        return false;
    }
    m_closed = false;
    NewSessionId();
    m_transport = &InTransport;
    m_server = std::make_unique<NetServer>( InTransport, MakeVersion(), InTickRate );
    m_server->SetTimeout( m_timeout );
    m_lagCompensation.SetTickRate( InTickRate );
    m_replicationServer = std::make_unique<NetReplicationServer>( GetWorld(), *m_server );
    m_replicationServer->Scripts = m_glue.State;
    m_replicationServer->AssignStaticIds();
    m_replicationServer->OnOtherMessage = [this]( NetServer::Client& InClient, uint8_t InType, BitReader& InReader, bool InReliable ) {
        if( InType == NetMessage::Rpc && InReliable )
        {
            OnRpc( InClient.Id, InReader );
        }
        else if( InType == NetMessage::KillcamCancel && InReliable )
        {
            OnKillcamCancel( InClient.Id, InReader );
        }
        else if( InType == NetMessage::NetDebugSubscribe && InReliable )
        {
            OnDebugSubscribe( InClient.Id, InReader );
        }
    };
    m_debugSubscribers.clear();
    UpdateAccounting();
    // Gameplay hears of a new client once replication has sent it the world.
    m_server->OnClientAccepted = [this, welcome = m_server->OnClientAccepted]( NetServer::Client& InClient ) {
        if( welcome )
        {
            welcome( InClient );
        }
        PushSessionEvent( NetSessionEvent::Kind::ClientJoined, InClient.Id );
    };
    // Remember who went, before replication forgets them.
    m_server->OnClientRemoved = [this, forget = m_server->OnClientRemoved]( NetServer::Client& InClient ) {
        RecordDeparture( InClient, m_lastNow );
        PushSessionEvent( NetSessionEvent::Kind::ClientLeft, InClient.Id, m_departures.back().Reason );
        if( m_debugSubscribers.erase( InClient.Id ) )
        {
            UpdateAccounting();
        }
        if( forget )
        {
            forget( InClient );
        }
    };
    return true;
}


bool NetCore::Connect( INetTransport& InTransport, NetAddress InServer, const NetClockSettings& InClock )
{
    if( IsActive() )
    {
        return false;
    }
    m_closed = false;
    NewSessionId();
    m_transport = &InTransport;
    m_client = std::make_unique<NetClient>( InTransport, InServer, MakeVersion(), InClock );
    m_client->SetTimeout( m_timeout );
    m_replicationClient = std::make_unique<NetReplicationClient>( GetWorld(), *m_client );
    m_replicationClient->Scripts = m_glue.State;
    m_replicationClient->AssignStaticIds();
    m_replicationClient->OnOwnerState = [this]( uint32_t InNetId, uint32_t InTick, const CharacterController::NetState& InState, const std::vector<uint8_t>& InScripts ) {
        if( m_predictor && m_replicationClient->FindEntity( InNetId ) == m_predictor->GetPawn() )
        {
            m_replicationClient->SetPredicted( InNetId );
            m_predictor->Reconcile( InTick, InState, InScripts );
        }
    };
    m_replicationClient->OnOwnerChanged = [this]( uint32_t InNetId, uint8_t InOwner ) { OnOwnerChanged( InNetId, InOwner ); };
    m_replicationClient->OnOtherMessage = [this]( uint8_t InType, BitReader& InReader, bool InReliable ) {
        if( InType == NetMessage::Rpc && InReliable )
        {
            OnRpc( 0, InReader );
        }
        else if( InType == NetMessage::NetDebug && InReliable )
        {
            OnDebugMessage( InReader );
        }
    };
    // NET-7: asked for again once this session is connected.
    m_debugAsked = NetDebugSubscription();
    m_shotLedger.Reset();
    m_shotBundles = 0;
    m_bandwidthReport = NetBandwidthReport();
    m_bandwidthReports = 0;
    // Killcams (ME-323) come as blobs.
    m_client->OnBlob = [this]( uint32_t, std::vector<uint8_t>& InData ) { OnBlob( InData ); };
    m_killcam.Reset();
    m_client->Connect();
    m_reportedClientState = m_client->GetState();
    return true;
}


void NetCore::PushSessionEvent( NetSessionEvent::Kind InType, uint8_t InClientId, std::string InReason )
{
    NetSessionEvent& event = m_sessionEvents.emplace_back();
    event.Type = InType;
    event.ClientId = InClientId;
    event.Reason = std::move( InReason );
    event.Source = GameWorld;
    if( m_server && IsRecording() && ( InType == NetSessionEvent::Kind::ClientJoined || InType == NetSessionEvent::Kind::ClientLeft ) )
    {
        const bool joined = InType == NetSessionEvent::Kind::ClientJoined;
        json payload;
        payload["client"] = InClientId;
        if( !joined )
        {
            payload["reason"] = event.Reason;
        }
        m_replayEvents.push_back( { { joined ? "net.joined" : "net.left", payload.dump(), 0 }, EntityHandle() } );
    }
}


void NetCore::FireSessionEvents()
{
    // A listener may Close (more events) or Shutdown: the outermost call delivers them all, in order.
    if( m_firingSessionEvents )
    {
        return;
    }
    m_firingSessionEvents = true;
    while( !m_sessionEvents.empty() )
    {
        for( NetSessionEvent& event : std::exchange( m_sessionEvents, {} ) )
        {
            event.Fire();
        }
    }
    m_firingSessionEvents = false;
}


void NetCore::NoteClientState()
{
    if( !m_client || m_client->GetState() == m_reportedClientState )
    {
        return;
    }
    const NetClient::State was = m_reportedClientState;
    m_reportedClientState = m_client->GetState();
    // Welcomed and gone between two looks: it was connected all the same.
    if( was == NetClient::State::Connecting && m_reportedClientState != NetClient::State::Connected && m_client->GetConnectedAt() >= 0.0 )
    {
        PushSessionEvent( NetSessionEvent::Kind::Connected, m_client->GetClientId() );
    }
    switch( m_reportedClientState )
    {
    case NetClient::State::Connected:
        PushSessionEvent( NetSessionEvent::Kind::Connected, m_client->GetClientId() );
        break;
    case NetClient::State::Rejected:
        PushSessionEvent( NetSessionEvent::Kind::Disconnected, 0, "rejected: " + m_client->GetRejectReason() );
        break;
    case NetClient::State::TimedOut:
        PushSessionEvent( NetSessionEvent::Kind::Disconnected, 0, "timed out" );
        break;
    case NetClient::State::Disconnected:
        if( was == NetClient::State::Connecting || was == NetClient::State::Connected )
        {
            PushSessionEvent( NetSessionEvent::Kind::Disconnected, 0, m_client->GetDisconnectReason() );
        }
        break;
    default:
        break;
    }
}


void NetCore::Predict( EntityHandle InPawn )
{
    PhysicsCore* physics = GetWorld().GetCore<PhysicsCore>();
    if( !m_client || !physics || !InPawn )
    {
        return;
    }
    const float tickSeconds = 1.f / static_cast<float>( std::max<uint16_t>( m_client->GetTickRate(), 1 ) );
    // A pawn it predicted before goes back to following snapshots.
    if( m_predictor && m_predictor->GetPawn() )
    {
        if( const NetIdentity* previous = m_predictor->GetPawn()->TryGetComponent<NetIdentity>() )
        {
            m_replicationClient->ClearPredicted( previous->GetNetId() );
        }
    }
    m_predictor = std::make_unique<NetPredictor>( *physics, InPawn, m_commandHandler, tickSeconds );
    // Its C# [Predicted] state: recorded, compared, restored and re-run with the mover (NET-6).
    if( m_glue.State.SavePredicted )
    {
        NetPredictor::ScriptHooks hooks;
        hooks.Save = [this, InPawn]( std::vector<uint8_t>& OutBytes ) {
            if( InPawn )
            {
                m_glue.State.SavePredicted( *InPawn.Get(), OutBytes );
            }
        };
        hooks.Restore = [this, InPawn]( const std::vector<uint8_t>& InBytes ) {
            if( InPawn && m_glue.RestorePredicted )
            {
                m_glue.RestorePredicted( *InPawn.Get(), InBytes );
            }
        };
        hooks.Simulate = [this, InPawn]() {
            if( InPawn && m_glue.SimulatePredicted )
            {
                m_glue.SimulatePredicted( *InPawn.Get() );
            }
        };
        m_predictor->SetScriptHooks( std::move( hooks ) );
    }
    if( const NetIdentity* identity = InPawn->TryGetComponent<NetIdentity>(); identity && identity->GetNetId() != 0 )
    {
        m_replicationClient->SetPredicted( identity->GetNetId() );
    }
}


void NetCore::OnOwnerChanged( uint32_t InNetId, uint8_t InOwner )
{
    if( !m_client )
    {
        return;
    }
    EntityHandle entity = m_replicationClient->FindEntity( InNetId );
    if( InOwner == 0 || InOwner != m_client->GetClientId() )
    {
        // Lost it: stop predicting and follow the server's snapshots again.
        if( m_predictor && entity && m_predictor->GetPawn() == entity )
        {
            m_predictor.reset();
            m_replicationClient->ClearPredicted( InNetId );
        }
        return;
    }
    // This client got a character: it predicts it.
    if( entity && entity->HasComponent<CharacterController>() && ( !m_predictor || m_predictor->GetPawn() != entity ) )
    {
        Predict( entity );
    }
}


bool NetCore::GetCommand( Entity& InEntity, InputCommand& OutCommand, InputCommand& OutPrevious ) const
{
    if( m_replayResim.Active && m_replayResim.Entity == InEntity.GetId().Value() )
    {
        OutCommand = m_replayResim.Command;
        OutPrevious = m_replayResim.Previous;
        return true;
    }
    if( m_server )
    {
        const NetIdentity* identity = InEntity.TryGetComponent<NetIdentity>();
        const NetServer::Client* client = identity && identity->GetOwner() != 0 ? m_server->FindClient( identity->GetOwner() ) : nullptr;
        if( !client )
        {
            return false;
        }
        OutCommand = client->Current;
        OutPrevious = client->Previous;
        return true;
    }
    if( m_predictor && m_predictor->GetPawn() && m_predictor->GetPawn()->GetId() == InEntity.GetId() )
    {
        OutCommand = m_predictor->GetCommand();
        OutPrevious = m_predictor->GetPreviousCommand();
        return true;
    }
    return false;
}


bool NetCore::SimulatesLocally( Entity& InEntity ) const
{
    // Not in the session (no NetIdentity, or a client-side copy that never got a NetId): local.
    // A replay viewer simulates none of the networked ones (ME-316).
    const NetIdentity* identity = InEntity.TryGetComponent<NetIdentity>();
    if( !IsClient() || !identity || identity->GetNetId() == 0 )
    {
        return true;
    }
    return m_predictor && m_predictor->GetPawn() && m_predictor->GetPawn()->GetId() == InEntity.GetId();
}


uint8_t NetCore::GetOwner( Entity& InEntity ) const
{
    const NetIdentity* identity = InEntity.TryGetComponent<NetIdentity>();
    return identity ? identity->GetOwner() : 0;
}


void NetCore::SetOwner( Entity& InEntity, uint8_t InClient )
{
    if( NetIdentity* identity = InEntity.TryGetComponent<NetIdentity>(); identity && m_server )
    {
        identity->SetOwner( InClient );
    }
}


bool NetCore::IsPredicted( Entity& InEntity ) const
{
    if( m_replayResim.Active && m_replayResim.Entity == InEntity.GetId().Value() )
    {
        return true;
    }
    return m_client && m_predictor && m_predictor->GetPawn() && m_predictor->GetPawn()->GetId() == InEntity.GetId();
}


uint32_t NetCore::GetNetIdOrAncestor( const EntityHandle& InEntity )
{
    const NetIdentity* identity = NetworkedSelfOrAncestor( InEntity );
    return identity ? identity->GetNetId() : 0;
}


uint32_t NetCore::GetNetId( Entity& InEntity ) const
{
    const NetIdentity* identity = InEntity.TryGetComponent<NetIdentity>();
    return identity ? identity->GetNetId() : 0;
}


EntityHandle NetCore::FindEntity( uint32_t InNetId ) const
{
    if( m_server )
    {
        return m_replicationServer->FindEntity( InNetId );
    }
    return m_replicationClient ? m_replicationClient->FindEntity( InNetId ) : EntityHandle();
}


bool NetCore::SendRpc( Entity& InEntity, int InRpcId, RpcKind InKind, const uint8_t* InData, size_t InSize )
{
    const uint32_t netId = GetNetId( InEntity );
    if( netId == 0 || InRpcId < 0 )
    {
        if( netId == 0 && InEntity.HasComponent<NetIdentity>() )
        {
            CLog::Log( CLog::LogType::Warning, "Net: RPC " + std::to_string( InRpcId ) + " on an entity without a NetId yet (spawned this tick?); dropped" );
        }
        return false;
    }
    BitWriter message;
    message.WriteBits( NetMessage::Rpc, 8 );
    message.WriteVarUInt( netId );
    message.WriteVarUInt( static_cast<uint64_t>( InRpcId ) );
    message.WriteVarUInt( InSize );
    message.WriteBytes( InData, InSize );
    if( message.GetByteCount() > NetConnection::kMaxMessageBytes )
    {
        CLog::Log( CLog::LogType::Warning, "Net: RPC " + std::to_string( InRpcId ) + " is " + std::to_string( message.GetByteCount() ) + " bytes, over the "
            + std::to_string( NetConnection::kMaxMessageBytes ) + "-byte message limit; dropped" );
        return false;
    }
    if( m_client && InKind == RpcKind::ToServer )
    {
        if( m_client->GetState() != NetClient::State::Connected )
        {
            return false;
        }
        m_client->Send( message, true );
        return true;
    }
    if( !m_server || InKind == RpcKind::ToServer )
    {
        return false;
    }
    const uint8_t owner = GetOwner( InEntity );
    bool sent = false;
    for( const auto& client : m_server->GetClients() )
    {
        if( client->Accepted && ( InKind == RpcKind::ToAll || client->Id == owner ) )
        {
            m_server->Send( *client, message, true );
            sent = true;
        }
    }
    return sent;
}


void NetCore::OnRpc( uint8_t InFromClient, BitReader& InReader )
{
    const uint32_t netId = static_cast<uint32_t>( InReader.ReadVarUInt() );
    const int rpcId = static_cast<int>( InReader.ReadVarUInt() );
    const uint64_t size = InReader.ReadVarUInt();
    if( InReader.IsOverflowed() || size > NetConnection::kMaxMessageBytes )
    {
        return;
    }
    std::vector<uint8_t> data( static_cast<size_t>( size ) );
    InReader.ReadBytes( data.data(), data.size() );
    EntityHandle entity = FindEntity( netId );
    if( InReader.IsOverflowed() || !entity || !m_glue.DispatchRpc )
    {
        return;
    }
    // A client may only call RPCs on what it owns.
    if( m_server && GetOwner( *entity.Get() ) != InFromClient )
    {
        return;
    }
    m_glue.DispatchRpc( *entity.Get(), rpcId, data.data(), data.size() );
}


LagCompensatedHit NetCore::LagCompensatedRaycast( const Vector3& InOrigin, const Vector3& InDirection, float InMaxDistance, const InputCommand& InCommand,
    const EntityHandle& InShooter, const EntityHandle& InInstigator )
{
    const LagCompensatedHit hit = m_lagCompensation.Raycast( GetWorld().GetCore<PhysicsCore>(), InOrigin, InDirection, InMaxDistance, InCommand.ViewTick, InShooter );
    if( m_server && IsRecording() )
    {
        RecordShot( InOrigin, InDirection, InMaxDistance, InCommand.ViewTick, InShooter, InInstigator, hit );
    }
    // NET-7: the shooter's client asked to see what the server made of its shots (a trigger pull,
    // not a projectile's sweep).
    if( m_server && !m_debugSubscribers.empty() && ( !InInstigator || InInstigator == InShooter ) )
    {
        SendShotResult( InOrigin, InDirection, InMaxDistance, InCommand, InShooter, hit );
    }
    return hit;
}


bool NetCore::StartRecording( const std::string& InPath, NetReplayHeader InHeader, const NetReplaySettings& InSettings )
{
    if( !m_server || IsRecording() )
    {
        return false;
    }
    const NetVersion& version = m_server->GetVersion();
    InHeader.TickRate = m_server->GetTickRate();
    InHeader.NetProtocol = version.Protocol;
    InHeader.BuildHash = version.Build;
    InHeader.ScriptSchemaHash = version.ScriptSchema;
    InHeader.SceneHash = version.Scene;
    InHeader.NetSchemaHash = NetSchema::Hash();
    auto recorder = std::make_unique<NetReplayRecorder>( std::move( InHeader ), InSettings );
    if( !InPath.empty() && !recorder->OpenFile( InPath ) )
    {
        return false;
    }
    m_recorder = std::move( recorder );
    m_replayShots.clear();
    m_replayEvents.clear();
    m_replayOwners.clear();
    m_replayMisses.clear();
    m_replayAlwaysRelevant.clear();
    m_ticksDropped = 0;
    // Who is here already.
    for( const auto& client : m_server->GetClients() )
    {
        if( client->Accepted )
        {
            json payload;
            payload["client"] = client->Id;
            m_replayEvents.push_back( { { "net.joined", payload.dump(), 0 }, EntityHandle() } );
        }
    }
    return true;
}


bool NetCore::StartPlayback( std::unique_ptr<INetReplaySource> InSource, std::string* OutError )
{
    auto fail = [OutError]( const std::string& InWhy ) {
        if( OutError )
        {
            *OutError = InWhy;
        }
        return false;
    };
    if( IsActive() )
    {
        return fail( "a session or a replay is running already" );
    }
    if( !InSource || InSource->GetChunkCount() == 0 )
    {
        return fail( "the replay has no ticks" );
    }
    const NetReplayHeader& header = InSource->GetHeader();
    const NetVersion version = MakeVersion();
    if( header.SceneHash != 0 && header.SceneHash != version.Scene )
    {
        return fail( "the replay was recorded on another scene (" + ( header.Map.empty() ? std::string( "unnamed" ) : header.Map )
            + "): its placed networked entities aren't this world's" );
    }
    if( header.NetSchemaHash != 0 && header.NetSchemaHash != NetSchema::Hash() )
    {
        CLog::Log( CLog::LogType::Warning, "Replay: recorded with other replicated C++ fields (NetSchema); their values may not read back" );
    }
    if( header.ScriptSchemaHash != 0 && header.ScriptSchemaHash != version.ScriptSchema )
    {
        CLog::Log( CLog::LogType::Warning, "Replay: recorded with other scripts or input actions; script fields may not read back" );
    }
    m_closed = false;
    NewSessionId();
    m_replaySource = std::move( InSource );
    m_replicationClient = std::make_unique<NetReplicationClient>( GetWorld() );
    m_replicationClient->Scripts = m_glue.State;
    m_replicationClient->AssignStaticIds();
    // The viewer owns nothing: owner changes reach NetIdentity, never a predictor.
    m_replicationClient->OnOwnerChanged = [this]( uint32_t InNetId, uint8_t InOwner ) { OnOwnerChanged( InNetId, InOwner ); };
    m_playback = std::make_unique<NetReplayPlayback>( *m_replaySource, *m_replicationClient );
    m_lagCompensation.SetTickRate( m_playback->GetTickRate() );
    m_tick = m_playback->GetTick();
    m_playbackError.clear();
    return true;
}


NetReplayPlayback::Check NetCore::CheckPlayback() const
{
    if( !m_playback )
    {
        NetReplayPlayback::Check check;
        check.Problems.push_back( "no replay is playing" );
        return check;
    }
    return m_playback->CheckWorld( &m_glue.State );
}


NetReplayResim::Result NetCore::ResimulateReplay( uint32_t InNetId, uint32_t InFrom, uint32_t InTo, const NetReplayResim::Settings& InSettings )
{
    OPTICK_EVENT( "NetCore::ResimulateReplay" );
    NetReplayResim::Result failed;
    failed.NetId = InNetId;
    failed.From = InFrom;
    failed.To = InTo;
    PhysicsCore* physics = GetWorld().GetCore<PhysicsCore>();
    EntityHandle entity = m_playback ? FindEntity( InNetId ) : EntityHandle();
    CharacterController* controller = entity ? entity->TryGetComponent<CharacterController>() : nullptr;
    if( !m_playback || !m_replaySource )
    {
        failed.Error = "no replay is playing";
        return failed;
    }
    if( !physics )
    {
        failed.Error = "the world has no physics";
        return failed;
    }
    if( !controller )
    {
        failed.Error = entity ? "#" + std::to_string( InNetId ) + " has no CharacterController" : "#" + std::to_string( InNetId ) + " isn't in the world at tick " + std::to_string( m_playback->GetTick() );
        return failed;
    }
    // Its own reader: the playback's keeps the chunk it shows.
    NetReplayReader reader( *m_replaySource );
    const float tickSeconds = 1.f / static_cast<float>( m_playback->GetTickRate() );
    const CharacterController::NetState before = controller->GetNetState();
    std::vector<uint8_t> predictedBefore;
    if( m_glue.State.SavePredicted )
    {
        m_glue.State.SavePredicted( *entity.Get(), predictedBefore );
    }
    // It pushes nothing: the dynamic bodies stay as the tick shown has them.
    const float pushBefore = controller->PushStrength;
    controller->PushStrength = 0.f;
    // The other characters stand where the recording has them each tick (their capsules are what it
    // collides with), and go back to the tick shown after.
    std::map<uint32_t, std::pair<CharacterController::NetState, Vector3>> othersBefore;     // and where its Transform was
    auto otherController = [this]( uint32_t InId ) -> CharacterController* {
        EntityHandle other = FindEntity( InId );
        return other ? other->TryGetComponent<CharacterController>() : nullptr;
    };

    NetReplayResim::Hooks hooks;
    hooks.Restore = [&]( const CharacterController::NetState& InState, const std::vector<uint8_t>& InPredicted ) {
        controller->SetNetState( InState );
        if( m_glue.RestorePredicted && !InPredicted.empty() )
        {
            m_glue.RestorePredicted( *entity.Get(), InPredicted );
        }
    };
    hooks.Step = [&]( uint32_t InTick, const InputCommand& InCommand, const InputCommand& InPrevious ) {
        m_replayResim.Active = true;
        m_replayResim.Entity = entity->GetId().Value();
        m_replayResim.Tick = InTick;
        m_replayResim.Command = InCommand;
        m_replayResim.Previous = InPrevious;
        if( m_commandHandler )
        {
            m_commandHandler( *entity.Get(), InCommand );
        }
        if( m_glue.SimulatePredicted )
        {
            m_glue.SimulatePredicted( *entity.Get() );
        }
        physics->StepCharacter( *entity.Get(), *controller, tickSeconds );
        m_replayResim.Active = false;
    };
    hooks.Save = [&]( CharacterController::NetState& OutState, std::vector<uint8_t>& OutPredicted ) {
        OutState = controller->GetNetState();
        if( m_glue.State.SavePredicted )
        {
            m_glue.State.SavePredicted( *entity.Get(), OutPredicted );
        }
    };
    hooks.PoseOthers = [&]( const std::vector<NetReplayMover>& InMovers ) {
        for( const NetReplayMover& mover : InMovers )
        {
            CharacterController* other = mover.NetId != InNetId ? otherController( mover.NetId ) : nullptr;
            if( other )
            {
                Transform* transform = other->Parent ? other->Parent->TryGetComponent<Transform>() : nullptr;
                othersBefore.try_emplace( mover.NetId, other->GetNetState(), transform ? transform->GetWorldPosition() : Vector3() );
                other->SetNetState( mover.State );
            }
        }
    };
    NetReplayResim::Result result = NetReplayResim::Run( reader, InNetId, InFrom, InTo, hooks, InSettings );
    m_replayResim = ReplayResimTick();
    // Back to the tick shown.
    for( const auto& [id, shown] : othersBefore )
    {
        if( CharacterController* other = otherController( id ) )
        {
            other->SetNetState( shown.first );
            if( Transform* transform = other->Parent ? other->Parent->TryGetComponent<Transform>() : nullptr )
            {
                transform->SetWorldPosition( shown.second );
            }
        }
    }
    controller->PushStrength = pushBefore;
    controller->SetNetState( before );
    if( m_glue.RestorePredicted && !predictedBefore.empty() )
    {
        m_glue.RestorePredicted( *entity.Get(), predictedBefore );
    }
    return result;
}


void NetCore::StopRecording()
{
    if( !IsRecording() )
    {
        return;
    }
    m_recorder->Close();
    if( !m_recorder->GetPath().empty() )
    {
        const uint64_t ticks = m_recorder->GetTicksRecorded();
        const uint16_t rate = std::max<uint16_t>( 1, m_recorder->GetHeader().TickRate );
        CLog::Log( m_recorder->HasFailed() ? CLog::LogType::Error : CLog::LogType::Info,
            "Replay: wrote " + m_recorder->GetPath() + ": " + std::to_string( ticks ) + " ticks (" + std::to_string( ticks / rate ) + " s) in "
                + std::to_string( m_recorder->GetChunksWritten() ) + " chunks, " + std::to_string( m_recorder->GetBytesWritten() / 1024 ) + " KB ("
                + std::to_string( m_recorder->GetRawBytes() / 1024 ) + " KB before deflate)" + ( m_recorder->HasFailed() ? ", then failed: " + m_recorder->GetError() : "" ) );
    }
    m_replayShots.clear();
    m_replayEvents.clear();
}


void NetCore::RecordReplayEvent( const std::string& InName, const std::string& InPayload, uint32_t InNetId )
{
    if( !IsRecording() )
    {
        return;
    }
    m_replayEvents.push_back( { { Cut( InName, NetReplayFormat::kMaxEventName ), Cut( InPayload, NetReplayFormat::kMaxEventPayload ), InNetId }, EntityHandle() } );
}


void NetCore::RecordReplayEvent( const std::string& InName, const std::string& InPayload, const EntityHandle& InEntity )
{
    if( !IsRecording() )
    {
        return;
    }
    // The id it has now (kept if it's destroyed before the tick is recorded); RecordReplayTick looks again.
    const NetIdentity* identity = NetworkedSelfOrAncestor( InEntity );
    m_replayEvents.push_back( { { Cut( InName, NetReplayFormat::kMaxEventName ), Cut( InPayload, NetReplayFormat::kMaxEventPayload ), identity ? identity->GetNetId() : 0u },
        InEntity } );
}


void NetCore::RecordShot( const Vector3& InOrigin, const Vector3& InDirection, float InMaxDistance, double InViewTick, const EntityHandle& InShooter,
    const EntityHandle& InInstigator, const LagCompensatedHit& InHit )
{
    NetReplayShot& shot = m_replayShots.emplace_back( MakeShot( InOrigin, InDirection, InMaxDistance, InViewTick, InShooter, InInstigator, InHit ) );
    if( InHit.Rejected )
    {
        return;
    }
    // The hitboxes as the shot was judged against them (all of them, not only the broadphase's).
    for( const NetLagCompensation::HitboxPose& pose : m_lagCompensation.GetPosesAt( InHit.JudgedTick ) )
    {
        if( IsSelfOrUnder( pose.Entity, InShooter ) )
        {
            continue;
        }
        shot.Hitboxes.push_back( ToReplayHitbox( pose ) );
    }
}


void NetCore::RecordReplayTick()
{
    NetReplayTick tick;
    tick.Tick = m_server->GetTick();
    tick.TicksOwed = m_ticksOwed;
    tick.TicksDropped = std::exchange( m_ticksDropped, 0u );
    tick.State = m_replicationServer->GetCurrent();
    for( const auto& client : m_server->GetClients() )
    {
        if( !client->Accepted )
        {
            continue;
        }
        NetReplayClient& record = tick.Clients.emplace_back();
        record.Id = client->Id;
        record.Command = client->Current;
        const uint64_t misses = client->Commands.GetMissedCount();
        auto known = m_replayMisses.find( client->Id );
        record.Missed = known != m_replayMisses.end() ? misses != known->second : misses > 0;
        m_replayMisses[client->Id] = misses;
        const NetStats& stats = client->Connection->GetStats();
        record.RoundTripMs = stats.RoundTripMs;
        record.PacketLoss = stats.PacketLoss;
        record.Slack = client->Commands.GetSlack();
        record.Misses = static_cast<uint32_t>( misses );
        record.LateCommands = static_cast<uint32_t>( client->Commands.GetLateArrivals() );
        record.Resyncs = static_cast<uint32_t>( client->Commands.GetRewindCount() );
    }
    // What NetIds are: new ones, and owners that changed.
    for( const auto& [id, state] : tick.State.Entities )
    {
        EntityHandle entity = m_replicationServer->FindEntity( id );
        const NetIdentity* identity = entity ? entity->TryGetComponent<NetIdentity>() : nullptr;
        const uint8_t owner = identity ? identity->GetOwner() : 0;
        auto known = m_replayOwners.find( id );
        if( known == m_replayOwners.end() || known->second != owner )
        {
            tick.Entities.push_back( { id, entity ? entity->GetName() : std::string(), identity ? identity->Prefab : std::string(), owner } );
            m_replayOwners[id] = owner;
        }
        if( identity && identity->AlwaysRelevant )
        {
            m_replayAlwaysRelevant[id] = tick.Tick;
        }
    }
    for( auto it = m_replayOwners.begin(); it != m_replayOwners.end(); )
    {
        it = tick.State.Entities.count( it->first ) ? std::next( it ) : m_replayOwners.erase( it );
    }
    // Every character's mover and [Predicted] state, as OwnerState sends them (full precision).
    for( const auto& [id, state] : tick.State.Entities )
    {
        EntityHandle entity = m_replicationServer->FindEntity( id );
        const CharacterController* controller = entity ? entity->TryGetComponent<CharacterController>() : nullptr;
        if( controller == nullptr )
        {
            continue;
        }
        NetReplayMover& mover = tick.Movers.emplace_back();
        mover.NetId = id;
        mover.State = controller->GetNetState();
        if( m_replicationServer->Scripts.SavePredicted )
        {
            m_replicationServer->Scripts.SavePredicted( *entity.Get(), mover.Predicted );
        }
    }
    tick.Shots = std::exchange( m_replayShots, {} );
    // Events about entities: CaptureTick has given this tick's new ones their NetIds.
    tick.Events.reserve( m_replayEvents.size() );
    for( PendingReplayEvent& pending : m_replayEvents )
    {
        const NetIdentity* identity = NetworkedSelfOrAncestor( pending.Entity );
        if( identity && identity->GetNetId() != 0 )
        {
            pending.Event.Entity = identity->GetNetId();
        }
        tick.Events.push_back( std::move( pending.Event ) );
    }
    m_replayEvents.clear();
    const uint32_t recorded = tick.Tick;
    m_recorder->Record( std::move( tick ) );
    // AlwaysRelevant NetIds as long as the ring has a tick they were so at (a killcam's view).
    const uint32_t oldest = m_recorder->GetRing().empty() ? recorded + 1 : m_recorder->GetRing().front()->Tick;
    for( auto it = m_replayAlwaysRelevant.begin(); it != m_replayAlwaysRelevant.end(); )
    {
        it = it->second < oldest ? m_replayAlwaysRelevant.erase( it ) : std::next( it );
    }
}


bool NetCore::SendKillcam( const EntityHandle& InVictim, const EntityHandle& InKiller, double InSeconds )
{
    const NetIdentity* victim = NetworkedSelfOrAncestor( InVictim );
    const NetIdentity* killer = NetworkedSelfOrAncestor( InKiller );
    if( !victim || !killer )
    {
        return false;
    }
    return SendKillcam( victim->GetOwner(), victim->GetNetId(), killer->GetNetId(), InSeconds );
}


bool NetCore::SendKillcam( uint8_t InClient, uint32_t InVictimNetId, uint32_t InKillerNetId, double InSeconds )
{
    if( !m_server || !IsRecording() || InClient == 0 || InKillerNetId == 0 || !( InSeconds > 0.0 ) )
    {
        return false;
    }
    const NetServer::Client* client = m_server->FindClient( InClient );
    const NetReplaySettings& settings = m_recorder->GetSettings();
    const uint16_t rate = std::max<uint16_t>( 1, m_server->GetTickRate() );
    const uint32_t ringTicks = static_cast<uint32_t>( std::max( 0.0, settings.RingSeconds * rate ) );
    if( !client || !client->Accepted || ringTicks == 0 )
    {
        return false;
    }
    ++m_killcamStats.Requested;
    // A newer one replaces what the client was still getting.
    for( auto it = m_killcams.begin(); it != m_killcams.end(); )
    {
        if( it->Client == InClient )
        {
            ++m_killcamStats.Cancelled;
            CancelKillcam( *it );
            it = m_killcams.erase( it );
        }
        else
        {
            ++it;
        }
    }
    // This tick (the one being simulated, or the next) is the death: it's cut once it's recorded.
    const uint32_t death = m_server->GetTick();
    const uint32_t ticks = std::min( ringTicks, static_cast<uint32_t>( std::max( 1.0, std::round( InSeconds * rate ) ) ) );
    KillcamSend& send = m_killcams.emplace_back();
    send.Client = InClient;
    send.Request.Id = m_nextKillcamId++;
    send.Request.VictimNetId = InVictimNetId;
    send.Request.KillerNetId = InKillerNetId;
    send.Request.DeathTick = death;
    send.Request.FirstTick = death >= ticks - 1 ? death - ( ticks - 1 ) : 0;
    send.Request.ChunkTicks = std::max<uint32_t>( 1, m_killcamSettings.ChunkTicks );
    send.Request.Codec = settings.Codec;
    return true;
}


void NetCore::CancelKillcam( KillcamSend& InSend )
{
    NetServer::Client* client = FindAccepted( m_server.get(), InSend.Client );
    if( client )
    {
        for( const uint32_t id : InSend.InFlight )
        {
            client->Connection->CancelBlob( id );
        }
    }
    // An encode under way finishes on the lane and is dropped with its future.
}


void NetCore::UpdateKillcams()
{
    if( m_killcams.empty() )
    {
        return;
    }
    OPTICK_EVENT( "NetCore::UpdateKillcams" );
    const uint32_t newest = m_recorder && !m_recorder->GetRing().empty() ? m_recorder->GetRing().back()->Tick : 0;
    for( auto it = m_killcams.begin(); it != m_killcams.end(); )
    {
        KillcamSend& send = *it;
        NetServer::Client* client = FindAccepted( m_server.get(), send.Client );
        if( !client )
        {
            ++m_killcamStats.Cancelled;
            CancelKillcam( send );
            it = m_killcams.erase( it );
            continue;
        }
        // Its death tick is in the ring: cut and encode it on the lane, from the ring's own ticks.
        if( !send.Encoding && !send.Streaming )
        {
            if( !m_recorder || !IsRecording() )
            {
                ++m_killcamStats.Failed;
                it = m_killcams.erase( it );
                continue;
            }
            if( newest < send.Request.DeathTick )
            {
                ++it;
                continue;
            }
            send.Request.RelevantRadius = m_replicationServer->Settings.RelevantRadius;
            // Every NetId AlwaysRelevant at a tick in the ring, alive now or not (a projectile gone by the death).
            send.Request.AlwaysRelevant.clear();
            for( const auto& entry : m_replayAlwaysRelevant )
            {
                send.Request.AlwaysRelevant.insert( entry.first );
            }
            send.Request.Header = m_recorder->GetHeader();
            std::vector<std::shared_ptr<const NetReplayTick>> ring;
            ring.reserve( m_recorder->GetRing().size() );
            for( const auto& tick : m_recorder->GetRing() )
            {
                if( tick->Tick > send.Request.DeathTick )
                {
                    break;
                }
                ring.push_back( tick );
            }
            send.Future = Jobs::BackgroundLane::Get().Submit( "Killcam Encode",
                [request = send.Request, ring = std::move( ring ), entities = m_recorder->GetRingEntities()]() { return NetKillcam::Encode( request, ring, entities ); } );
            send.Encoding = true;
        }
        if( send.Encoding )
        {
            if( send.Future.wait_for( std::chrono::seconds( 0 ) ) != std::future_status::ready )
            {
                ++it;
                continue;
            }
            NetKillcam::Encoded encoded = send.Future.get();
            send.Encoding = false;
            if( encoded.Blobs.empty() )
            {
                ++m_killcamStats.Failed;
                CLog::Log( CLog::LogType::Warning, "Net: killcam " + std::to_string( send.Request.Id ) + " for client " + std::to_string( send.Client ) + ": " + encoded.Error );
                it = m_killcams.erase( it );
                continue;
            }
            ++m_killcamStats.Encoded;
            m_killcamStats.Bytes += encoded.Bytes;
            m_killcamStats.Ticks += encoded.Ticks;
            m_killcamStats.EncodeMs += encoded.Milliseconds;
            CLog::Log( CLog::LogType::Info, "Net: killcam " + std::to_string( send.Request.Id ) + " for client " + std::to_string( send.Client ) + ": victim #"
                + std::to_string( send.Request.VictimNetId ) + ", killer #" + std::to_string( send.Request.KillerNetId ) + ", ticks " + std::to_string( encoded.FirstTick )
                + "-" + std::to_string( encoded.LastTick ) + ", " + std::to_string( encoded.Bytes / 1024 ) + " KB in " + std::to_string( encoded.Blobs.size() )
                + " chunks (" + std::to_string( encoded.RawBytes / 1024 ) + " KB before deflate; encoded in " + std::to_string( static_cast<int>( encoded.Milliseconds + 0.5 ) )
                + " ms)" );
            send.Queued.assign( std::make_move_iterator( encoded.Blobs.begin() ), std::make_move_iterator( encoded.Blobs.end() ) );
            send.Streaming = true;
        }
        // Streaming: the blobs still on their way, then the next ones in order while there's room.
        std::set<uint32_t> onTheirWay;
        for( const NetBlobProgress& progress : client->Connection->GetBlobProgress() )
        {
            if( progress.Outgoing )
            {
                onTheirWay.insert( progress.Id );
            }
        }
        send.InFlight.erase( std::remove_if( send.InFlight.begin(), send.InFlight.end(), [&]( uint32_t InId ) { return !onTheirWay.count( InId ); } ),
            send.InFlight.end() );
        bool refused = false;
        // The first chunk goes alone, so it has the whole share and the victim can start on it; once
        // it's acked the rest go at once (the channel sends four at a time, in order, the rest queued).
        const bool firstOnItsWay = send.FirstId != 0 && std::find( send.InFlight.begin(), send.InFlight.end(), send.FirstId ) != send.InFlight.end();
        while( !send.Queued.empty() && !firstOnItsWay )
        {
            const uint32_t id = m_server->SendBlob( *client, send.Queued.front() );
            if( id == 0 )
            {
                refused = true;
                break;
            }
            send.InFlight.push_back( id );
            send.Queued.pop_front();
            if( send.FirstId == 0 )
            {
                send.FirstId = id;
                break;
            }
        }
        if( refused )
        {
            ++m_killcamStats.Failed;
            CLog::Log( CLog::LogType::Warning, "Net: killcam " + std::to_string( send.Request.Id ) + ": the blob channel refused a chunk" );
            CancelKillcam( send );
            it = m_killcams.erase( it );
            continue;
        }
        if( send.Queued.empty() && send.InFlight.empty() )
        {
            ++m_killcamStats.Completed;
            it = m_killcams.erase( it );
            continue;
        }
        ++it;
    }
}


void NetCore::OnBlob( std::vector<uint8_t>& InData )
{
    // NET-7: the server's results for this client's shots.
    if( NetDebugMessages::IsShotBundle( InData.data(), InData.size() ) )
    {
        std::vector<NetShotResult> results;
        if( !NetDebug::kAvailable || !NetDebugMessages::DecodeShots( InData.data(), InData.size(), results ) )
        {
            CLog::Log( CLog::LogType::Warning, "Net: a shot-result bundle from the server doesn't read (another engine's net debug or replay format?)" );
            return;
        }
        for( NetShotResult& result : results )
        {
            m_shotLedger.AddResult( std::move( result ), m_lastNow );
        }
        ++m_shotBundles;
        return;
    }
    const uint32_t before = m_killcam.GetId();
    const bool wasComplete = m_killcam.IsComplete();
    switch( m_killcam.Accept( InData.data(), InData.size() ) )
    {
    case NetKillcamReceiver::Result::Bad:
        CLog::Log( CLog::LogType::Warning, "Net: a killcam blob from the server doesn't read: " + m_killcam.GetError() );
        return;
    case NetKillcamReceiver::Result::Accepted:
        if( m_killcam.IsComplete() && ( !wasComplete || before != m_killcam.GetId() ) )
        {
            CLog::Log( CLog::LogType::Info, "Net: killcam " + std::to_string( m_killcam.GetId() ) + " received: victim #" + std::to_string( m_killcam.GetVictimNetId() )
                + ", killer #" + std::to_string( m_killcam.GetKillerNetId() ) + ", ticks " + std::to_string( m_killcam.GetFirstTick() ) + "-"
                + std::to_string( m_killcam.GetDeathTick() ) + ", " + std::to_string( m_killcam.GetBytesReceived() / 1024 ) + " KB in "
                + std::to_string( m_killcam.GetChunkCount() ) + " chunks" );
        }
        return;
    default:
        return;
    }
}


bool NetCore::CancelKillcamDownload()
{
    if( !m_client || m_killcam.GetId() == 0 || m_killcam.IsComplete() || m_killcam.IsCancelled() )
    {
        return false;
    }
    m_killcam.Cancel();
    if( m_client->GetState() != NetClient::State::Connected )
    {
        return false;
    }
    BitWriter message;
    message.WriteBits( NetMessage::KillcamCancel, 8 );
    message.WriteVarUInt( m_killcam.GetId() );
    m_client->Send( message, true );
    CLog::Log( CLog::LogType::Info, "Net: killcam " + std::to_string( m_killcam.GetId() ) + " cancelled with " + std::to_string( m_killcam.GetChunksReceived() ) + " of "
        + std::to_string( m_killcam.GetChunkCount() ) + " chunks in (" + std::to_string( m_killcam.GetBytesReceived() / 1024 ) + " KB)" );
    return true;
}


void NetCore::OnKillcamCancel( uint8_t InClient, BitReader& InReader )
{
    const uint32_t id = static_cast<uint32_t>( InReader.ReadVarUInt() );
    if( InReader.IsOverflowed() )
    {
        return;
    }
    for( auto it = m_killcams.begin(); it != m_killcams.end(); ++it )
    {
        if( it->Client == InClient && it->Request.Id == id )
        {
            ++m_killcamStats.CancelledByClient;
            CLog::Log( CLog::LogType::Info, "Net: killcam " + std::to_string( id ) + " for client " + std::to_string( InClient ) + " cancelled by the client: "
                + std::to_string( it->InFlight.size() ) + " chunks on their way and " + std::to_string( it->Queued.size() ) + " queued dropped"
                + ( it->Encoding ? " (still encoding)" : "" ) );
            CancelKillcam( *it );
            m_killcams.erase( it );
            return;
        }
    }
    // Already whole (the cancel crossed its last ack), or replaced: nothing left to stop.
}


float NetCore::GetKillcamProgress() const
{
    if( m_killcam.GetId() == 0 )
    {
        return 0.f;
    }
    if( m_killcam.IsComplete() )
    {
        return 1.f;
    }
    if( m_killcam.GetTotalBytes() == 0 )
    {
        return 0.f;
    }
    // The blobs whole, and what has come of the ones on their way.
    size_t done = m_killcam.GetBytesReceived();
    if( m_client )
    {
        for( const NetBlobProgress& progress : m_client->GetConnection().GetBlobProgress() )
        {
            if( !progress.Outgoing )
            {
                done += progress.DoneBytes;
            }
        }
    }
    return std::min( 0.99f, static_cast<float>( static_cast<double>( done ) / static_cast<double>( m_killcam.GetTotalBytes() ) ) );
}


void NetCore::SetDebug( const NetDebugSubscription& InDebug )
{
    m_debug = NetDebug::kAvailable ? InDebug : NetDebugSubscription();
    UpdateAccounting();
}


void NetCore::SetDebugClientsAllowed( bool InAllowed )
{
    m_debugClientsAllowed = NetDebug::kAvailable && InAllowed;
    if( !m_debugClientsAllowed && !m_debugSubscribers.empty() )
    {
        for( const auto& [id, subscriber] : m_debugSubscribers )
        {
            if( m_replicationServer )
            {
                m_replicationServer->CloseBandwidthWindow( id );
            }
        }
        m_debugSubscribers.clear();
        UpdateAccounting();
    }
}


void NetCore::UpdateAccounting()
{
    if( !m_replicationServer )
    {
        return;
    }
    bool count = NetDebug::kAvailable && m_debug.Bandwidth;
    for( const auto& [id, subscriber] : m_debugSubscribers )
    {
        count = count || subscriber.Wants.Bandwidth;
    }
    m_replicationServer->Settings.AccountEntities = count;
}


void NetCore::OnDebugSubscribe( uint8_t InClient, BitReader& InReader )
{
    NetDebugSubscription wants;
    if( !NetDebug::kAvailable || !NetDebugMessages::ReadSubscribe( InReader, wants ) || !m_server )
    {
        return;
    }
    if( !m_debugClientsAllowed )
    {
        if( wants.Any() && m_debugStats.Refused++ == 0 )
        {
            CLog::Log( CLog::LogType::Info, "Net: client " + std::to_string( InClient )
                + "'s Net Debugger asked for shots / bandwidth; this server doesn't share them (--net-debug-clients allows it)" );
        }
        return;
    }
    ++m_debugStats.Subscriptions;
    auto known = m_debugSubscribers.find( InClient );
    const bool hadBandwidth = known != m_debugSubscribers.end() && known->second.Wants.Bandwidth;
    if( !wants.Any() )
    {
        m_debugSubscribers.erase( InClient );
    }
    else
    {
        DebugSubscriber& subscriber = m_debugSubscribers[InClient];
        // A bandwidth window starts now (counting for this client from here on).
        if( wants.Bandwidth && !subscriber.Wants.Bandwidth )
        {
            subscriber.WindowStart = m_server->GetTick();
            m_replicationServer->TakeBandwidthWindow( InClient, 0, 0.0 );
        }
        subscriber.Wants = wants;
    }
    if( hadBandwidth && !wants.Bandwidth )
    {
        m_replicationServer->CloseBandwidthWindow( InClient );
    }
    UpdateAccounting();
    CLog::Log( CLog::LogType::Info, std::string( "Net: client " ) + std::to_string( InClient ) + "'s Net Debugger asks for" + ( wants.Shots ? " shots" : "" )
        + ( wants.Bandwidth ? " bandwidth" : "" ) + ( wants.Any() ? "" : " nothing" ) );
}


void NetCore::SendShotResult( const Vector3& InOrigin, const Vector3& InDirection, float InMaxDistance, const InputCommand& InCommand, const EntityHandle& InShooter,
    const LagCompensatedHit& InHit )
{
    const NetIdentity* shooter = NetworkedSelfOrAncestor( InShooter );
    auto subscriber = shooter ? m_debugSubscribers.find( shooter->GetOwner() ) : m_debugSubscribers.end();
    if( subscriber == m_debugSubscribers.end() || !subscriber->second.Wants.Shots )
    {
        return;
    }
    NetShotResult result;
    result.CommandTick = InCommand.Tick;
    result.Shot = MakeShot( InOrigin, InDirection, InMaxDistance, InCommand.ViewTick, InShooter, EntityHandle(), InHit );
    if( !InHit.Rejected )
    {
        // The target's hitboxes (or the near miss's) as the shot was judged against them (the very
        // tick the raycast tested), and live.
        const std::vector<NetLagCompensation::HitboxPose> judged = m_lagCompensation.GetPosesAt( InHit.JudgedTick );
        const std::vector<NetLagCompensation::HitboxPose> live = m_lagCompensation.GetPosesAt( static_cast<double>( m_lagCompensation.GetNewestTick() ) );
        const uint32_t shown = PickShownEntity( judged, InOrigin, InDirection, InMaxDistance, result.Shot.Target, InShooter );
        for( const NetLagCompensation::HitboxPose* pose : ShownPoses( judged, shown, InHit.Entity, InShooter ) )
        {
            result.Shot.Hitboxes.push_back( ToReplayHitbox( *pose ) );
            for( const NetLagCompensation::HitboxPose& now : live )
            {
                if( now.Entity == pose->Entity )
                {
                    result.Live.push_back( ToReplayHitbox( now ) );
                    break;
                }
            }
        }
    }
    // Queued for the client's next bundle (UpdateDebug), never ahead of its live traffic.
    BitWriter measure;
    NetDebugMessages::WriteShot( measure, result );
    DebugSubscriber& to = subscriber->second;
    if( to.PendingBytes + measure.GetByteCount() > NetDebug::kMaxPendingShotBytes )
    {
        ++m_debugStats.ShotResultsDropped;
        return;
    }
    to.PendingBytes += measure.GetByteCount();
    to.PendingShots.push_back( std::move( result ) );
}


void NetCore::NotePredictedShot( const Vector3& InOrigin, const Vector3& InDirection, float InMaxDistance, const EntityHandle& InShooter, const LagCompensatedHit& InHit,
    const NetLagCompensation& InPresent )
{
    // Only the shots of a predicted tick as it first runs: a resim's are the same shots again.
    if( !NetDebug::kAvailable || !m_inTick || IsResimulating() || !m_predictor || !m_client || m_client->GetState() != NetClient::State::Connected )
    {
        return;
    }
    const NetIdentity* shooter = NetworkedSelfOrAncestor( InShooter );
    if( !shooter || shooter->GetNetId() != GetNetIdOrAncestor( m_predictor->GetPawn() ) )
    {
        return;
    }
    const float length = InDirection.Length();
    if( length < 1e-9f )
    {
        return;
    }
    NetPredictedShot shot;
    shot.Tick = m_tick;
    shot.Shooter = shooter->GetNetId();
    shot.Origin = InOrigin;
    shot.Direction = InDirection * ( 1.f / length );
    shot.Range = InMaxDistance;
    shot.ViewTick = m_predictor->GetCommand().ViewTick;
    shot.Hit = InHit.Hit;
    shot.Occluded = InHit.Occluded;
    shot.Point = InHit.Point;
    shot.Distance = InHit.Distance;
    if( InHit.Entity )
    {
        shot.Target = GetNetIdOrAncestor( InHit.Entity );
        shot.TargetPart = InHit.Entity->GetName();
    }
    const std::vector<NetLagCompensation::HitboxPose> poses = InPresent.GetPosesAt( 0.0 );
    const uint32_t shown = PickShownEntity( poses, InOrigin, InDirection, InMaxDistance, shot.Target, InShooter );
    for( const NetLagCompensation::HitboxPose* pose : ShownPoses( poses, shown, InHit.Entity, InShooter ) )
    {
        shot.Hitboxes.push_back( ToReplayHitbox( *pose ) );
    }
    m_shotLedger.AddPredicted( std::move( shot ), m_lastNow );
}


void NetCore::OnDebugMessage( BitReader& InReader )
{
    uint8_t kind = 0;
    NetBandwidthReport bandwidth;
    if( !NetDebug::kAvailable || !NetDebugMessages::ReadDebug( InReader, kind, bandwidth ) )
    {
        return;
    }
    if( kind == NetDebug::Bandwidth )
    {
        for( NetEntityBytes& entity : bandwidth.Top )
        {
            const EntityHandle handle = FindEntity( entity.NetId );
            entity.Name = handle ? handle->GetName() : std::string();
        }
        m_bandwidthReport = std::move( bandwidth );
        ++m_bandwidthReports;
    }
}


void NetCore::UpdateDebug()
{
    if( !NetDebug::kAvailable || m_debugSubscribers.empty() )
    {
        return;
    }
    const uint32_t tick = m_server->GetTick();
    const uint32_t rate = std::max<uint32_t>( 1, m_server->GetTickRate() );
    for( auto& [id, subscriber] : m_debugSubscribers )
    {
        NetServer::Client* client = nullptr;
        for( const auto& candidate : m_server->GetClients() )
        {
            if( candidate->Id == id && candidate->Accepted )
            {
                client = candidate.get();
            }
        }
        if( !client )
        {
            continue;
        }
        // Shots: every result since the last bundle, once that one has landed (one on its way at a
        // time: a burst waits as one bundle instead of taking the channel's slots from a killcam).
        if( subscriber.ShotBlob != 0 )
        {
            bool onItsWay = false;
            for( const NetBlobProgress& progress : client->Connection->GetBlobProgress() )
            {
                onItsWay = onItsWay || ( progress.Outgoing && progress.Id == subscriber.ShotBlob );
            }
            subscriber.ShotBlob = onItsWay ? subscriber.ShotBlob : 0;
        }
        if( subscriber.ShotBlob == 0 && !subscriber.PendingShots.empty() )
        {
            const std::vector<uint8_t> bundle = NetDebugMessages::EncodeShots( subscriber.PendingShots );
            subscriber.ShotBlob = m_server->SendBlob( *client, bundle );
            if( subscriber.ShotBlob != 0 )
            {
                ++m_debugStats.ShotBundles;
                m_debugStats.ShotBundleBytes += bundle.size();
                m_debugStats.ShotResults += subscriber.PendingShots.size();
            }
            else
            {
                m_debugStats.ShotResultsDropped += subscriber.PendingShots.size();
            }
            subscriber.PendingShots.clear();
            subscriber.PendingBytes = 0;
        }
        // Bandwidth: the window, once a second.
        if( !subscriber.Wants.Bandwidth || tick - subscriber.WindowStart < rate )
        {
            continue;
        }
        const NetBandwidthReport report = m_replicationServer->TakeBandwidthWindow( id, NetDebug::kTopEntities, static_cast<double>( tick - subscriber.WindowStart ) / rate );
        BitWriter message;
        NetDebugMessages::WriteBandwidth( message, report );
        m_server->Send( *client, message, true );
        ++m_debugStats.BandwidthReports;
        subscriber.WindowStart = tick;
    }
}


std::vector<NetEntityBytes> NetCore::GetTopEntities( size_t InCount, double InSeconds ) const
{
    std::vector<NetEntityBytes> top;
    if( !m_replicationServer )
    {
        return top;
    }
    for( const auto& [id, bits] : m_replicationServer->GetEntityBits() )
    {
        NetEntityBytes& entity = top.emplace_back();
        entity.NetId = id;
        entity.Bits = bits;
        entity.BytesPerSecond = InSeconds > 0.0 ? static_cast<double>( bits ) / 8.0 / InSeconds : 0.0;
        entity.Name = m_replicationServer->GetEntityName( id );
    }
    std::sort( top.begin(), top.end(), []( const NetEntityBytes& InA, const NetEntityBytes& InB ) { return InA.Bits != InB.Bits ? InA.Bits > InB.Bits : InA.NetId < InB.NetId; } );
    if( top.size() > InCount )
    {
        top.resize( InCount );
    }
    return top;
}


LagCompensatedHit NetCore::PresentRaycast( const Vector3& InOrigin, const Vector3& InDirection, float InMaxDistance, const EntityHandle& InShooter )
{
    // One recorded frame of the hitboxes as they are, judged at that frame.
    NetLagCompensation present;
    present.Record( GetWorld(), 0 );
    const LagCompensatedHit hit = present.Raycast( GetWorld().GetCore<PhysicsCore>(), InOrigin, InDirection, InMaxDistance, -1.0, InShooter );
    if( m_client && m_debug.Shots )
    {
        NotePredictedShot( InOrigin, InDirection, InMaxDistance, InShooter, hit, present );
    }
    return hit;
}


float NetCore::GetRoundTripMs( uint8_t InClient ) const
{
    if( m_client )
    {
        return m_client->GetConnection().GetStats().RoundTripMs;
    }
    const NetServer::Client* client = m_server ? m_server->FindClient( InClient ) : nullptr;
    return client && client->Connection ? client->Connection->GetStats().RoundTripMs : 0.f;
}


bool NetCore::IsSessionLive() const
{
    if( m_closed )
    {
        return false;
    }
    if( m_server )
    {
        return true;
    }
    return m_client && ( m_client->GetState() == NetClient::State::Connecting || m_client->GetState() == NetClient::State::Connected );
}


void NetCore::Close( double InNow )
{
    m_lastNow = InNow;
    m_closing = true;
    m_closed = true;
    if( m_server )
    {
        m_server->Close( InNow );
    }
    if( m_client )
    {
        m_client->Disconnect( InNow );
        NoteClientState();
    }
    if( m_transport )
    {
        m_transport->Update( InNow + kReleaseEverything );
    }
    m_closing = false;
    FireSessionEvents();
}


void NetCore::RecordDeparture( const NetServer::Client& InClient, double InNow )
{
    Departure departure;
    departure.Id = InClient.Id;
    departure.Address = InClient.Address;
    departure.Reason = m_closing ? "closed" : InClient.Left ? "left" : "timed out";
    departure.ConnectedSeconds = std::max( 0.0, InNow - InClient.AcceptedAt );
    departure.Stats = InClient.Connection->GetStats();
    departure.MissedCommands = InClient.Commands.GetMissedCount();
    departure.LateCommands = InClient.Commands.GetLateArrivals();
    if( m_replicationServer )
    {
        departure.Snapshots = m_replicationServer->GetClientSnapshotsSent( InClient.Id );
        departure.SnapshotBytes = m_replicationServer->GetClientSnapshotBytes( InClient.Id );
    }
    m_departures.push_back( departure );
}


bool NetCore::WriteReport( const std::string& InPath, double InNow ) const
{
    json report;
    const double uptime = m_startedAt >= 0.0 ? InNow - m_startedAt : 0.0;
    report["Uptime"] = uptime;
    if( m_server )
    {
        report["Role"] = "server";
        report["TickRate"] = m_server->GetTickRate();
        report["Tick"] = m_server->GetTick();
        report["ClientsNow"] = m_server->GetClientCount();
        report["Accepted"] = m_server->GetAcceptedCount();
        report["Disconnects"] = m_server->GetDisconnectCount();
        report["Timeouts"] = m_server->GetTimeoutCount();
        report["Rejected"] = m_server->GetRejectedCount();
        report["LastRejection"] = m_server->GetLastRejection();
        report["SnapshotsSent"] = m_replicationServer->GetSnapshotsSent();
        report["SnapshotBytes"] = m_replicationServer->GetSnapshotBytes();
        report["Networked"] = m_replicationServer->GetCurrent().Entities.size();
        json clients = json::array();
        auto describe = []( const Departure& InClient ) {
            json client;
            client["Id"] = InClient.Id;
            client["Address"] = InClient.Address;
            client["Status"] = InClient.Reason;
            client["ConnectedSeconds"] = InClient.ConnectedSeconds;
            client["Connection"] = StatsJson( InClient.Stats, InClient.ConnectedSeconds );
            client["Snapshots"] = InClient.Snapshots;
            client["SnapshotRate"] = InClient.ConnectedSeconds > 0.0 ? static_cast<double>( InClient.Snapshots ) / InClient.ConnectedSeconds : 0.0;
            client["SnapshotBytes"] = InClient.SnapshotBytes;
            client["MissedCommands"] = InClient.MissedCommands;
            client["LateCommands"] = InClient.LateCommands;
            return client;
        };
        for( const Departure& departure : m_departures )
        {
            clients.push_back( describe( departure ) );
        }
        for( const auto& client : m_server->GetClients() )
        {
            if( client->Accepted )
            {
                Departure current;
                current.Id = client->Id;
                current.Address = client->Address;
                current.Reason = "connected";
                current.ConnectedSeconds = std::max( 0.0, InNow - client->AcceptedAt );
                current.Stats = client->Connection->GetStats();
                current.Snapshots = m_replicationServer->GetClientSnapshotsSent( client->Id );
                current.SnapshotBytes = m_replicationServer->GetClientSnapshotBytes( client->Id );
                current.MissedCommands = client->Commands.GetMissedCount();
                current.LateCommands = client->Commands.GetLateArrivals();
                clients.push_back( describe( current ) );
            }
        }
        report["Clients"] = clients;
        if( m_recorder )
        {
            json replay;
            replay["Path"] = m_recorder->GetPath();
            replay["Ticks"] = m_recorder->GetTicksRecorded();
            replay["Chunks"] = m_recorder->GetChunksWritten();
            replay["Bytes"] = m_recorder->GetBytesWritten();
            replay["RawBytes"] = m_recorder->GetRawBytes();
            replay["Closed"] = m_recorder->IsClosed();
            replay["Error"] = m_recorder->GetError();
            report["Replay"] = replay;
        }
        json killcams;
        killcams["Requested"] = m_killcamStats.Requested;
        killcams["Encoded"] = m_killcamStats.Encoded;
        killcams["Completed"] = m_killcamStats.Completed;
        killcams["Failed"] = m_killcamStats.Failed;
        killcams["Cancelled"] = m_killcamStats.Cancelled;
        killcams["CancelledByClient"] = m_killcamStats.CancelledByClient;
        killcams["Bytes"] = m_killcamStats.Bytes;
        killcams["Ticks"] = m_killcamStats.Ticks;
        killcams["EncodeMs"] = m_killcamStats.EncodeMs;
        killcams["Pending"] = m_killcams.size();
        report["Killcams"] = killcams;
        // NET-7: the snapshot bytes by entity (while counting: --net-report, or a Net Debugger asking).
        if( m_replicationServer->GetAccountedBytes() > 0 )
        {
            json accounting;
            uint64_t entityBits = m_replicationServer->GetDespawnedBits();
            for( const auto& [id, bits] : m_replicationServer->GetEntityBits() )
            {
                entityBits += bits;
            }
            accounting["AccountedBytes"] = m_replicationServer->GetAccountedBytes();
            accounting["EntityBits"] = entityBits;     // the despawned ones' too
            accounting["DespawnedBits"] = m_replicationServer->GetDespawnedBits();
            accounting["OverheadBits"] = m_replicationServer->GetOverheadBits();
            accounting["Entities"] = m_replicationServer->GetEntityBits().size();
            report["SnapshotAccounting"] = accounting;
            json top = json::array();
            for( const NetEntityBytes& entity : GetTopEntities( 10, uptime ) )
            {
                top.push_back( json{ { "NetId", entity.NetId }, { "Name", entity.Name }, { "Bytes", entity.Bits / 8 }, { "BytesPerSecond", entity.BytesPerSecond } } );
            }
            report["TopEntities"] = top;
        }
        json debug;
        debug["Subscribers"] = m_debugSubscribers.size();
        debug["Subscriptions"] = m_debugStats.Subscriptions;
        debug["Refused"] = m_debugStats.Refused;
        debug["ClientsAllowed"] = m_debugClientsAllowed;
        debug["ShotResults"] = m_debugStats.ShotResults;
        debug["ShotResultsDropped"] = m_debugStats.ShotResultsDropped;
        debug["ShotBundles"] = m_debugStats.ShotBundles;
        debug["ShotBundleBytes"] = m_debugStats.ShotBundleBytes;
        debug["BandwidthReports"] = m_debugStats.BandwidthReports;
        report["NetDebug"] = debug;
    }
    else if( m_playback )
    {
        report["Role"] = "replay";
        report["TickRate"] = m_playback->GetTickRate();
        report["Tick"] = m_playback->GetTick();
        report["FirstTick"] = m_playback->GetFirstTick();
        report["LastTick"] = m_playback->GetLastTick();
        report["TicksFed"] = m_playback->GetTicksFed();
        report["Rebuilds"] = m_playback->GetRebuilds();
        report["Mirrored"] = m_replicationClient->GetEntityCount();
        report["Error"] = m_playback->GetError();
    }
    else if( m_client )
    {
        const double connectedAt = m_client->GetConnectedAt();
        const double endedAt = m_client->GetEndedAt() >= 0.0 ? m_client->GetEndedAt() : InNow;
        const double connectedSeconds = connectedAt >= 0.0 ? std::max( 0.0, endedAt - connectedAt ) : 0.0;
        report["Role"] = "client";
        report["State"] = StateName( m_client->GetState() );
        report["Connected"] = connectedAt >= 0.0;
        report["ConnectedSeconds"] = connectedSeconds;
        report["Timeouts"] = m_client->GetState() == NetClient::State::TimedOut ? 1 : 0;
        report["RejectReason"] = m_client->GetRejectReason();
        report["DisconnectReason"] = m_client->GetDisconnectReason();
        report["ClientId"] = m_client->GetClientId();
        report["TickRate"] = m_client->GetTickRate();
        report["Tick"] = m_client->GetClock().GetTick();
        report["ServerTick"] = m_client->GetServerTick();
        report["ServerSlack"] = m_client->GetServerSlack();
        report["TargetSlack"] = m_client->GetClock().GetTargetSlack();
        report["Dilation"] = m_client->GetClock().GetDilation();
        report["Resyncs"] = m_client->GetClock().GetResyncCount();
        report["Stalls"] = m_client->GetStallCount();
        report["Outages"] = m_client->GetOutageCount();
        report["Hitches"] = m_client->GetHitchCount();
        report["HitchMisses"] = m_client->GetHitchMisses();
        report["Connection"] = StatsJson( m_client->GetConnection().GetStats(), connectedSeconds );
        report["SnapshotsReceived"] = m_replicationClient->GetSnapshotsReceived();
        const double span = m_lastSnapshotAt - m_firstSnapshotAt;
        report["SnapshotRate"] = span > 0.0 ? static_cast<double>( m_lastSnapshotCount - m_firstSnapshotCount ) / span : 0.0;
        report["Mirrored"] = m_replicationClient->GetEntityCount();
        report["RenderDelayTicks"] = m_replicationClient->GetDelayTicks();
        if( m_killcam.GetId() != 0 )
        {
            json killcam;
            killcam["Id"] = m_killcam.GetId();
            killcam["Complete"] = m_killcam.IsComplete();
            killcam["Chunks"] = m_killcam.GetChunkCount();
            killcam["ChunksReceived"] = m_killcam.GetChunksReceived();
            killcam["Bytes"] = m_killcam.GetBytesReceived();
            killcam["TotalBytes"] = m_killcam.GetTotalBytes();
            killcam["Victim"] = m_killcam.GetVictimNetId();
            killcam["Killer"] = m_killcam.GetKillerNetId();
            killcam["FirstTick"] = m_killcam.GetFirstTick();
            killcam["DeathTick"] = m_killcam.GetDeathTick();
            report["Killcam"] = killcam;
        }
        if( m_predictor )
        {
            report["Corrections"] = m_predictor->GetCorrectionCount();
            // NET-7: the misprediction timeline's mismatches (the last 10 s), what each found.
            json mispredicts = json::array();
            for( const NetMispredictRecord& record : m_predictor->GetMispredicts() )
            {
                mispredicts.push_back( json{ { "Tick", record.Tick }, { "InitialSync", record.InitialSync }, { "PositionDeltaMm", record.PositionDelta().Length() * 1000.0 },
                    { "ScriptRanges", record.ScriptDiffs.size() }, { "Resimulated", record.Resimulated.size() }, { "Describe", record.Describe() } } );
            }
            report["Mispredicts"] = mispredicts;
        }
        // NET-7: the Net Debugger's shots, the client's prediction against the server's word, and
        // the server's newest top-bandwidth report.
        if( m_debug.Shots )
        {
            const NetShotLedger::Totals& totals = m_shotLedger.GetTotals();
            json shots;
            shots["Predicted"] = totals.Predicted;
            shots["Results"] = totals.Results;
            shots["Paired"] = totals.Paired;
            shots["Agreed"] = totals.Agreed;
            shots["SamePart"] = totals.SamePart;
            shots["Disagreed"] = totals.Disagreed;
            shots["ClientOnly"] = totals.ClientOnly;
            shots["ServerOnly"] = totals.ServerOnly;
            shots["Pending"] = m_shotLedger.GetPendingCount();
            shots["Bundles"] = m_shotBundles;
            shots["Agreement"] = totals.Paired > 0 ? static_cast<double>( totals.Agreed ) / static_cast<double>( totals.Paired ) : 0.0;
            shots["MeanPointDeltaMm"] = totals.PointPairs > 0 ? totals.SumPointDelta / static_cast<double>( totals.PointPairs ) * 1000.0 : 0.0;
            shots["MaxPointDeltaMm"] = totals.MaxPointDelta * 1000.0;
            json recent = json::array();
            const auto& settled = m_shotLedger.GetRecent();
            for( size_t i = settled.size() > 16 ? settled.size() - 16 : 0; i < settled.size(); ++i )
            {
                const NetShotComparison& shot = settled[i];
                recent.push_back( json{ { "Tick", shot.HasClient ? shot.Client.Tick : shot.Server.CommandTick }, { "Agrees", shot.Agrees() },
                    { "RewoundTicks", shot.HasServer ? shot.Server.Shot.RewoundTicks : 0.0 }, { "Describe", shot.Describe() } } );
            }
            shots["Recent"] = recent;
            report["Shots"] = shots;
        }
        if( m_debug.Bandwidth )
        {
            json bandwidth;
            bandwidth["Reports"] = m_bandwidthReports;
            bandwidth["Tick"] = m_bandwidthReport.Tick;
            bandwidth["Seconds"] = m_bandwidthReport.Seconds;
            bandwidth["SnapshotBytes"] = m_bandwidthReport.SnapshotBytes;
            bandwidth["OverheadBits"] = m_bandwidthReport.OverheadBits;
            json top = json::array();
            for( const NetEntityBytes& entity : m_bandwidthReport.Top )
            {
                top.push_back( json{ { "NetId", entity.NetId }, { "Name", entity.Name }, { "Bits", entity.Bits }, { "BytesPerSecond", entity.BytesPerSecond } } );
            }
            bandwidth["Top"] = top;
            report["TopEntities"] = bandwidth;
        }
    }
    else
    {
        report["Role"] = "none";
    }

    std::ofstream out( InPath );
    if( !out )
    {
        CLog::Log( CLog::LogType::Error, "Net: can't write the report to " + InPath );
        return false;
    }
    out << report.dump( 4 ) << std::endl;
    CLog::Log( CLog::LogType::Info, "Net: wrote the report to " + InPath );
    return true;
}


void NetCore::NewSessionId()
{
    static std::atomic<uint32_t> s_next{ 1 };
    m_sessionId = s_next.fetch_add( 1, std::memory_order_relaxed );
}


void NetCore::Shutdown()
{
    EndPresentation();
    m_sessionId = 0;
    StopRecording();
    m_recorder.reset();
    m_killcams.clear();
    m_killcamStats = KillcamStats();
    m_killcam.Reset();
    m_playback.reset();
    m_replaySource.reset();
    m_playbackError.clear();
    m_closed = false;
    m_predictor.reset();
    m_replicationServer.reset();
    m_replicationClient.reset();
    m_server.reset();
    m_client.reset();
    m_transport = nullptr;
    m_departures.clear();
    m_sessionEvents.clear();
    m_reportedClientState = NetClient::State::Disconnected;
    m_startedAt = -1.0;
    m_firstSnapshotAt = -1.0;
    m_lastSnapshotAt = -1.0;
    m_firstSnapshotCount = 0;
    m_lastSnapshotCount = 0;
    m_debugSubscribers.clear();
    m_debugStats = DebugStats();
    m_debugAsked = NetDebugSubscription();
    m_shotLedger.Reset();
    m_shotBundles = 0;
    m_bandwidthReport = NetBandwidthReport();
    m_bandwidthReports = 0;
    m_inTick = false;
}


void NetCore::Receive( double InNow )
{
    EndPresentation();
    m_lastNow = InNow;
    if( !IsActive() )
    {
        return;
    }
    if( m_startedAt < 0.0 )
    {
        m_startedAt = InNow;
    }
    if( m_transport )
    {
        m_transport->Update( InNow );
    }
    if( m_server )
    {
        m_server->Receive( InNow );
    }
    if( m_client )
    {
        m_client->Receive( InNow );
        NoteClientState();
        // NET-7: what the Net Debugger wants, asked for once connected and whenever it changes.
        if( NetDebug::kAvailable && m_client->GetState() == NetClient::State::Connected && m_debugAsked != m_debug )
        {
            BitWriter message;
            NetDebugMessages::WriteSubscribe( message, m_debug );
            m_client->Send( message, true );
            m_debugAsked = m_debug;
        }
        m_shotLedger.Update( InNow );
        const uint64_t received = m_replicationClient->GetSnapshotsReceived();
        if( received > m_lastSnapshotCount )
        {
            if( m_firstSnapshotAt < 0.0 )
            {
                m_firstSnapshotAt = InNow;
                m_firstSnapshotCount = received;
            }
            m_lastSnapshotAt = InNow;
            m_lastSnapshotCount = received;
        }
    }
    FireSessionEvents();
}


void NetCore::BeginTick( uint32_t InTicksOwed )
{
    EndPresentation();
    if( m_playback )
    {
        m_tick = m_playback->GetTick();
    }
    if( m_server )
    {
        m_tick = m_server->GetTick();
        m_ticksOwed = InTicksOwed;
        m_server->BeginTick( InTicksOwed );
        // Each client's command drives what it owns.
        if( m_commandHandler )
        {
            for( Entity& entity : GetEntities() )
            {
                const uint8_t owner = entity.GetComponent<NetIdentity>().GetOwner();
                if( const NetServer::Client* client = owner != 0 ? m_server->FindClient( owner ) : nullptr )
                {
                    m_commandHandler( entity, client->Current );
                }
            }
        }
    }
    m_inTick = false;
    if( m_client && m_client->GetState() == NetClient::State::Connected )
    {
        m_tick = m_client->GetClock().GetTick();
        m_inTick = true;
        // High ping (NET-15): keep the view within what the server will rewind to (its cap, less
        // two ticks for the view's quantization and arrival); past it, remote movers extrapolate.
        m_replicationClient->SetMinRenderTick( static_cast<double>( m_tick ) - ( m_lagCompensation.MaxRewindSeconds * m_client->GetTickRate() - 2.0 ) );
        // What the player is looking at: lag compensation rewinds shots to it.
        if( m_replicationClient->GetSnapshotsReceived() > 0 )
        {
            m_localCommand.ViewTick = m_replicationClient->GetRenderTick();
        }
        m_client->SubmitCommand( m_localCommand );
        if( m_predictor )
        {
            // Predict with the command exactly as the server will see it.
            InputCommand quantized = m_localCommand;
            quantized.Tick = m_tick;
            for( float& axis : quantized.Axes )
            {
                axis = NetCommands::QuantizeAxis( axis );
            }
            quantized.ViewTick = NetCommands::QuantizeViewTick( m_tick, quantized.ViewTick );
            quantized.Yaw = quantized.HasAim ? NetCommands::QuantizeYaw( quantized.Yaw ) : 0.f;
            quantized.Pitch = quantized.HasAim ? NetCommands::QuantizePitch( quantized.Pitch ) : 0.f;
            m_predictor->Apply( quantized );
        }
    }
}


void NetCore::EndTick( double InNow )
{
    if( m_server )
    {
        m_lagCompensation.Record( GetWorld(), m_server->GetTick() );
        m_replicationServer->CaptureTick( m_server->GetTick() );
        if( IsRecording() )
        {
            RecordReplayTick();
        }
        // Killcams cut from the ticks recorded so far, and their next chunks.
        UpdateKillcams();
        // NET-7: the Net Debuggers' bandwidth windows.
        UpdateDebug();
        m_server->EndTick( InNow );
        m_transport->Update( InNow );
    }
    if( m_predictor && m_client && m_client->GetState() == NetClient::State::Connected )
    {
        m_predictor->Record( m_tick );
    }
    m_inTick = false;
}


void NetCore::Flush( double InNow )
{
    if( m_client )
    {
        if( m_client->IsSendDue( InNow ) )
        {
            m_replicationClient->BeforeFlush();
        }
        m_client->Flush( InNow );
        m_transport->Update( InNow );
    }
}


void NetCore::WriteInterpolated( double InFrameSeconds )
{
    if( m_playback )
    {
        // The playhead moves, the replay's messages go in, then the client poses the world.
        if( !m_playback->Update( InFrameSeconds ) && m_playbackError != m_playback->GetError() )
        {
            m_playbackError = m_playback->GetError();
            CLog::Log( CLog::LogType::Error, "Replay: " + m_playbackError );
        }
        m_replicationClient->Update( InFrameSeconds );
        m_tick = m_playback->GetTick();
        // Each character's recorded [Predicted] script state at the tick shown (format 3 movers):
        // what the server and the owner had, which snapshots don't carry. Every frame, so an entity
        // spawned by a seek gets it once its script is up.
        if( const NetReplayTick* record = m_playback->GetRecord(); record && m_glue.RestorePredicted )
        {
            for( const NetReplayMover& mover : record->Movers )
            {
                EntityHandle entity = mover.Predicted.empty() ? EntityHandle() : FindEntity( mover.NetId );
                if( entity )
                {
                    m_glue.RestorePredicted( *entity.Get(), mover.Predicted );
                }
            }
        }
        return;
    }
    if( m_client && m_client->GetState() == NetClient::State::Connected )
    {
        m_replicationClient->Update( InFrameSeconds );
        if( m_predictor )
        {
            m_predictor->UpdateSmoothing( InFrameSeconds );
        }
    }
}


void NetCore::BeginPresentation()
{
    EndPresentation();
    if( !m_client || !m_predictor || !m_predictor->GetPawn() )
    {
        return;
    }
    const Vector3 offset = m_predictor->GetVisualOffset();
    Transform* transform = offset.IsZero() ? nullptr : m_predictor->GetPawn()->TryGetComponent<Transform>();
    if( !transform )
    {
        return;
    }
    m_presentation.Active = true;
    m_presentation.Pawn = m_predictor->GetPawn();
    m_presentation.Simulated = transform->GetPosition();
    transform->SetWorldPosition( transform->GetWorldPosition() + offset );
    m_presentation.Presented = transform->GetPosition();
}


void NetCore::EndPresentation()
{
    if( !m_presentation.Active )
    {
        return;
    }
    m_presentation.Active = false;
    Transform* transform = m_presentation.Pawn ? m_presentation.Pawn->TryGetComponent<Transform>() : nullptr;
    // Back to the simulated pose bit for bit (the local position, so a parent's matrix can't round
    // it), unless something moved it meanwhile.
    const Vector3* now = transform ? &transform->GetPosition() : nullptr;
    if( now && now->x == m_presentation.Presented.x && now->y == m_presentation.Presented.y && now->z == m_presentation.Presented.z )
    {
        transform->SetPosition( m_presentation.Simulated );
    }
    m_presentation.Pawn = EntityHandle();
}


double NetCore::GetClockScale() const
{
    return m_client && m_client->GetState() == NetClient::State::Connected ? m_client->GetClock().GetScale() : 1.0;
}


#if USING( ME_EDITOR )
void NetCore::OnEditorInspect()
{
    Base::OnEditorInspect();
    if( m_server )
    {
        ImGui::Text( "Server: tick %u, %zu clients, %zu networked", m_server->GetTick(), m_server->GetClientCount(), m_replicationServer->GetCurrent().Entities.size() );
        for( const auto& client : m_server->GetClients() )
        {
            const NetStats& stats = client->Connection->GetStats();
            ImGui::Text( "  #%u: %.0f ms, %.1f%% loss, slack %.2f, %llu missed", client->Id, stats.RoundTripMs, stats.PacketLoss * 100.f,
                client->Commands.GetSlack(), static_cast<unsigned long long>( client->Commands.GetMissedCount() ) );
        }
    }
    else if( m_client )
    {
        const NetStats& stats = m_client->GetConnection().GetStats();
        ImGui::Text( "Client #%u: tick %u, %.0f ms, %.1f%% loss", m_client->GetClientId(), m_client->GetClock().GetTick(), stats.RoundTripMs, stats.PacketLoss * 100.f );
        ImGui::Text( "Slack %.2f (target %.2f), dilation %+.1f%%, %d resyncs, %llu stalls, %llu outages", m_client->GetServerSlack(), m_client->GetClock().GetTargetSlack(),
            m_client->GetClock().GetDilation() * 100.f, m_client->GetClock().GetResyncCount(), static_cast<unsigned long long>( m_client->GetStallCount() ),
            static_cast<unsigned long long>( m_client->GetOutageCount() ) );
        ImGui::Text( "Own hitches: %llu (%llu misses, not counted against the link)", static_cast<unsigned long long>( m_client->GetHitchCount() ),
            static_cast<unsigned long long>( m_client->GetHitchMisses() ) );
        ImGui::Text( "%zu mirrored, render tick %.1f (delay %.2f)", m_replicationClient->GetEntityCount(), m_replicationClient->GetRenderTick(), m_replicationClient->GetDelayTicks() );
        if( m_predictor )
        {
            ImGui::Text( "Prediction: %d corrections, %d matches, last resim %d ticks in %.3f ms", m_predictor->GetCorrectionCount(), m_predictor->GetMatchCount(),
                m_predictor->GetLastResimTicks(), m_predictor->GetLastResimMilliseconds() );
        }
        if( m_client->GetState() == NetClient::State::Rejected )
        {
            ImGui::TextUnformatted( ( "Rejected: " + m_client->GetRejectReason() ).c_str() );
        }
    }
    else
    {
        ImGui::TextUnformatted( "No session" );
    }
}
#endif
