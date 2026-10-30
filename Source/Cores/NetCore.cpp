#include "PCH.h"
#include "Cores/NetCore.h"
#include "Components/Networking/NetIdentity.h"
#include "Components/Transform.h"
#include "Cores/PhysicsCore.h"
#include "Engine/ProjectSettings.h"
#include "Engine/World.h"
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
#if USING( ME_SCRIPTING )
    std::vector<std::string> scripts;
    for( int i = 0; i < ScriptEngine::GetScriptCount(); ++i )
    {
        scripts.push_back( ScriptEngine::GetScriptName( i ) );
    }
    std::sort( scripts.begin(), scripts.end() );
    uint64_t schema = NetVersion::Hash( "" );
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
    m_replicationServer->AssignStaticIds();
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
    m_replicationClient->AssignStaticIds();
    m_replicationClient->OnOwnerState = [this]( uint32_t InNetId, uint32_t InTick, const CharacterController::NetState& InState ) {
        if( m_predictor && m_replicationClient->FindEntity( InNetId ) == m_predictor->GetPawn() )
        {
            m_replicationClient->SetPredicted( InNetId );
            m_predictor->Reconcile( InTick, InState );
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
    m_predictor = std::make_unique<NetPredictor>( *physics, InPawn, m_commandHandler, tickSeconds );
    if( const NetIdentity* identity = InPawn->TryGetComponent<NetIdentity>(); identity && identity->GetNetId() != 0 )
    {
        m_replicationClient->SetPredicted( identity->GetNetId() );
    }
}


LagCompensatedHit NetCore::LagCompensatedRaycast( const Vector3& InOrigin, const Vector3& InDirection, float InMaxDistance, const InputCommand& InCommand,
    const EntityHandle& InShooter )
{
    return m_lagCompensation.Raycast( GetWorld().GetCore<PhysicsCore>(), InOrigin, InDirection, InMaxDistance, InCommand.ViewTick, InShooter );
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
        m_replicationClient->BeforeFlush();
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
