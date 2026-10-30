#include "PCH.h"
#include "Cores/NetCore.h"
#include "Mods/Mods.h"
#include "Components/Networking/NetIdentity.h"
#include "Components/Transform.h"
#include "Cores/PhysicsCore.h"
#include "Engine/ProjectSettings.h"
#include "Engine/World.h"
#include "Net/BitStream.h"
#include "CLog.h"
#include "JSON.h"
#include <algorithm>
#include <fstream>

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
        return out;
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
    };
    // Remember who went, before replication forgets them.
    m_server->OnClientRemoved = [this, forget = m_server->OnClientRemoved]( NetServer::Client& InClient ) {
        RecordDeparture( InClient, m_lastNow );
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
    };
    m_client->Connect();
    return true;
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
    const NetIdentity* identity = InEntity.TryGetComponent<NetIdentity>();
    if( !m_client || !identity || identity->GetNetId() == 0 )
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
    return m_client && m_predictor && m_predictor->GetPawn() && m_predictor->GetPawn()->GetId() == InEntity.GetId();
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
    return m_client ? m_replicationClient->FindEntity( InNetId ) : EntityHandle();
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
    const EntityHandle& InShooter )
{
    return m_lagCompensation.Raycast( GetWorld().GetCore<PhysicsCore>(), InOrigin, InDirection, InMaxDistance, InCommand.ViewTick, InShooter );
}


LagCompensatedHit NetCore::PresentRaycast( const Vector3& InOrigin, const Vector3& InDirection, float InMaxDistance, const EntityHandle& InShooter )
{
    // One recorded frame of the hitboxes as they are, judged at that frame.
    NetLagCompensation present;
    present.Record( GetWorld(), 0 );
    return present.Raycast( GetWorld().GetCore<PhysicsCore>(), InOrigin, InDirection, InMaxDistance, -1.0, InShooter );
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


void NetCore::Close( double InNow )
{
    m_lastNow = InNow;
    m_closing = true;
    if( m_server )
    {
        m_server->Close( InNow );
    }
    if( m_client )
    {
        m_client->Disconnect( InNow );
    }
    if( m_transport )
    {
        m_transport->Update( InNow + kReleaseEverything );
    }
    m_closing = false;
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
        report["Connection"] = StatsJson( m_client->GetConnection().GetStats(), connectedSeconds );
        report["SnapshotsReceived"] = m_replicationClient->GetSnapshotsReceived();
        const double span = m_lastSnapshotAt - m_firstSnapshotAt;
        report["SnapshotRate"] = span > 0.0 ? static_cast<double>( m_lastSnapshotCount - m_firstSnapshotCount ) / span : 0.0;
        report["Mirrored"] = m_replicationClient->GetEntityCount();
        report["RenderDelayTicks"] = m_replicationClient->GetDelayTicks();
        if( m_predictor )
        {
            report["Corrections"] = m_predictor->GetCorrectionCount();
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


void NetCore::Shutdown()
{
    m_predictor.reset();
    m_replicationServer.reset();
    m_replicationClient.reset();
    m_server.reset();
    m_client.reset();
    m_transport = nullptr;
    m_departures.clear();
    m_startedAt = -1.0;
    m_firstSnapshotAt = -1.0;
    m_lastSnapshotAt = -1.0;
    m_firstSnapshotCount = 0;
    m_lastSnapshotCount = 0;
}


void NetCore::Receive( double InNow )
{
    m_lastNow = InNow;
    if( !IsActive() )
    {
        return;
    }
    if( m_startedAt < 0.0 )
    {
        m_startedAt = InNow;
    }
    m_transport->Update( InNow );
    if( m_server )
    {
        m_server->Receive( InNow );
    }
    if( m_client )
    {
        m_client->Receive( InNow );
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
}


void NetCore::BeginTick()
{
    if( m_server )
    {
        m_tick = m_server->GetTick();
        m_server->BeginTick();
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
    if( m_client && m_client->GetState() == NetClient::State::Connected )
    {
        m_tick = m_client->GetClock().GetTick();
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
        m_server->EndTick( InNow );
        m_transport->Update( InNow );
    }
    if( m_predictor && m_client && m_client->GetState() == NetClient::State::Connected )
    {
        m_predictor->Record( m_tick );
    }
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
    if( m_client && m_client->GetState() == NetClient::State::Connected )
    {
        m_replicationClient->Update( InFrameSeconds );
        if( m_predictor )
        {
            m_predictor->UpdateSmoothing( InFrameSeconds );
        }
    }
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
        ImGui::Text( "Slack %.2f, dilation %+.1f%%, %d resyncs", m_client->GetServerSlack(), m_client->GetClock().GetDilation() * 100.f, m_client->GetClock().GetResyncCount() );
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
