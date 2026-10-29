#include "PCH.h"
#include "Cores/NetCore.h"
#include "Components/Networking/NetIdentity.h"
#include "Components/Transform.h"
#include "Engine/ProjectSettings.h"
#include "Engine/World.h"
#include <algorithm>

#if USING( ME_SCRIPTING )
#include "Scripting/ScriptEngine.h"
#endif

#if USING( ME_EDITOR )
#include <imgui.h>
#endif


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
    m_server = std::make_unique<NetServer>( InTransport, MakeVersion(), InTickRate );
    m_replicationServer = std::make_unique<NetReplicationServer>( GetWorld(), *m_server );
    m_replicationServer->AssignStaticIds();
    return true;
}


bool NetCore::Connect( INetTransport& InTransport, NetAddress InServer, const NetClockSettings& InClock )
{
    if( IsActive() )
    {
        return false;
    }
    m_client = std::make_unique<NetClient>( InTransport, InServer, MakeVersion(), InClock );
    m_replicationClient = std::make_unique<NetReplicationClient>( GetWorld(), *m_client );
    m_replicationClient->AssignStaticIds();
    m_client->Connect();
    return true;
}


void NetCore::Shutdown()
{
    m_replicationServer.reset();
    m_replicationClient.reset();
    m_server.reset();
    m_client.reset();
}


void NetCore::Receive( double InNow )
{
    if( m_server )
    {
        m_server->Receive( InNow );
    }
    if( m_client )
    {
        m_client->Receive( InNow );
    }
}


void NetCore::BeginTick()
{
    if( m_server )
    {
        m_tick = m_server->GetTick();
        m_server->BeginTick();
    }
    if( m_client && m_client->GetState() == NetClient::State::Connected )
    {
        m_tick = m_client->GetClock().GetTick();
        m_client->SubmitCommand( m_localCommand );
    }
}


void NetCore::EndTick( double InNow )
{
    if( m_server )
    {
        m_replicationServer->CaptureTick( m_server->GetTick() );
        m_server->EndTick( InNow );
    }
}


void NetCore::Flush( double InNow )
{
    if( m_client )
    {
        m_replicationClient->BeforeFlush();
        m_client->Flush( InNow );
    }
}


void NetCore::WriteInterpolated( double InFrameSeconds )
{
    if( m_client && m_client->GetState() == NetClient::State::Connected )
    {
        m_replicationClient->Update( InFrameSeconds );
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
