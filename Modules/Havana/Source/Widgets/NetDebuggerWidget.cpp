#include "Widgets/NetDebuggerWidget.h"

#if USING( ME_EDITOR )

#include "CLog.h"
#include "Cores/NetCore.h"
#include "EditorApp.h"
#include "Editor/NetPlay.h"
#include "Engine/Engine.h"
#include "JSON.h"
#include "Net/NetReplication.h"
#include "Net/NetSession.h"
#include <ImGuiTheme.h>
#include <imgui.h>
#include <optick.h>
#include <algorithm>
#include <chrono>

namespace
{
    NetDebuggerWidget* s_instance = nullptr;
    // How often each child is asked for its view.
    constexpr double kViewEvery = 0.25;
    // An answer this late is given up on (the child busy, or gone).
    constexpr double kViewTimeout = 3.0;
}


NetDebuggerWidget::NetDebuggerWidget( EditorApp* InApp )
    : HavanaWidget( "Net Debugger" )
    , m_app( InApp )
{
    s_instance = this;
    IsOpen = false;
}


NetDebuggerWidget::~NetDebuggerWidget()
{
    if( s_instance == this )
    {
        s_instance = nullptr;
    }
}


NetDebuggerWidget* NetDebuggerWidget::Get()
{
    return s_instance;
}


double NetDebuggerWidget::Now()
{
    return std::chrono::duration<double>( std::chrono::steady_clock::now().time_since_epoch() ).count();
}


void NetDebuggerWidget::Init()
{
}


void NetDebuggerWidget::Destroy()
{
}


void NetDebuggerWidget::Update()
{
}


bool NetDebuggerWidget::GetClientView( int InIndex, NetDebuggerView& OutView, double& OutReceivedAt ) const
{
    const auto it = m_clients.find( InIndex );
    if( it == m_clients.end() || !it->second.HasView )
    {
        return false;
    }
    OutView = it->second.View;
    OutReceivedAt = it->second.ReceivedAt;
    return true;
}


bool NetDebuggerWidget::GetLastExport( int InIndex, std::string& OutAnswer, uint32_t& OutTick ) const
{
    const auto it = m_clients.find( InIndex );
    if( it == m_clients.end() || !it->second.HasExport )
    {
        return false;
    }
    OutAnswer = it->second.UI.LastExport;
    OutTick = it->second.ExportTick;
    return true;
}


void NetDebuggerWidget::Poll()
{
    OPTICK_EVENT( "NetDebuggerWidget::Poll" );
    NetPlay& play = NetPlay::Get();
    const double now = Now();
    // A new session starts afresh.
    if( play.IsHosting() && m_hostingSince < 0.0 )
    {
        m_hostingSince = now;
        m_clients.clear();
    }
    else if( !play.IsHosting() )
    {
        m_hostingSince = -1.0;
    }
    NetCore* net = GetEngine().Net;
    NetServer* server = net && play.IsHosting() ? net->GetServer() : nullptr;
    for( const NetPlay::ChildInfo& child : play.GetChildren() )
    {
        ClientState& state = m_clients[child.Index];
        if( state.Pid != child.Pid )
        {
            // Restarted: another process, another client. It starts with its own overlays on: the
            // panel's setting, when it is off, is sent to it.
            const bool overlays = state.UI.DrawOverlays;
            state = ClientState();
            state.Pid = child.Pid;
            state.UI.DrawOverlays = overlays;
            state.OverlaysPending = overlays != NetDebuggerUI().DrawOverlays;
        }
        if( !child.Running )
        {
            state.ViewId = state.ExportId = state.OverlaysId = 0;
            continue;
        }
        if( state.ViewId != 0 )
        {
            std::string answer;
            if( play.TakeReply( child.Index, state.ViewId, answer ) )
            {
                state.ViewId = 0;
                const json document = json::parse( answer, nullptr, false );
                NetDebuggerView view;
                std::string error;
                if( !document.is_discarded() && NetDebuggerView::FromJson( document, view, &error ) )
                {
                    state.View = std::move( view );
                    state.HasView = true;
                    state.ReceivedAt = now;
                    state.Error.clear();
                    state.ClientHistory.Add( state.View, now );
                }
                else
                {
                    state.Error = document.is_discarded() ? "its answer isn't JSON: " + answer.substr( 0, 160 ) : "its answer: " + error;
                }
            }
            else if( now - state.AskedAt > kViewTimeout )
            {
                // Still waited for, not asked again: a child that stopped reading (a breakpoint,
                // SIGSTOP) has one view queued on its stdin, not one every quarter second.
                state.Error = "no answer for " + std::to_string( static_cast<int>( now - state.AskedAt ) ) + " s";
            }
        }
        if( state.ViewId == 0 && ( state.AskedAt < 0.0 || now - state.AskedAt >= kViewEvery ) )
        {
            state.ViewId = play.SendControl( child.Index, "net view " + std::to_string( state.UI.HasSelected ? state.UI.Selected : 0u ) );
            state.AskedAt = now;
        }
        if( state.ExportId != 0 )
        {
            std::string answer;
            if( play.TakeReply( child.Index, state.ExportId, answer ) )
            {
                state.ExportId = 0;
                state.HasExport = true;
                state.UI.LastExport = answer;
                CLog::Log( CLog::LogType::Info, "Net Debugger: client " + std::to_string( child.Index ) + " > net export " + std::to_string( state.ExportTick ) + ": " + answer );
            }
        }
        if( state.OverlaysId != 0 )
        {
            std::string answer;
            if( play.TakeReply( child.Index, state.OverlaysId, answer ) )
            {
                state.OverlaysId = 0;
                CLog::Log( CLog::LogType::Info, "Net Debugger: client " + std::to_string( child.Index ) + ": " + answer );
            }
        }
        // The newest setting, once the one before it was answered.
        if( state.OverlaysPending && state.OverlaysId == 0 )
        {
            state.OverlaysId = play.SendControl( child.Index, std::string( "net overlays " ) + ( state.UI.DrawOverlays ? "on" : "off" ) );
            state.OverlaysPending = state.OverlaysId == 0;
        }
        // The server's end of its link.
        const NetServer::Client* connection = server && child.ClientId > 0 ? server->FindClient( static_cast<uint8_t>( child.ClientId ) ) : nullptr;
        if( connection && connection->Connection )
        {
            const NetStats& stats = connection->Connection->GetStats();
            state.ServerRate.Sample( stats, now );
            NetDebuggerHistory::Sample sample;
            sample.RoundTripMs = stats.RoundTripMs;
            sample.LossPercent = stats.PacketLoss * 100.f;
            sample.KbpsIn = static_cast<float>( state.ServerRate.KbpsIn );
            sample.KbpsOut = static_cast<float>( state.ServerRate.KbpsOut );
            sample.Slack = connection->Commands.GetSlack();
            state.ServerHistory.Add( sample, now );
        }
    }
}


void NetDebuggerWidget::Render()
{
    if( !IsOpen )
    {
        return;
    }
    OPTICK_EVENT( "NetDebuggerWidget::Render" );
    Poll();
    ImGui::SetNextWindowSize( ImVec2( ImGuiTheme::Em( 78.f ), ImGuiTheme::Em( 46.f ) ), ImGuiCond_FirstUseEver );
    if( !ImGui::Begin( Name.c_str(), &IsOpen ) )
    {
        ImGui::End();
        return;
    }
    if( ImGui::BeginChild( "##NetSession", ImVec2( ImGuiTheme::Em( 30.f ), 0.f ), ImGuiChildFlags_ResizeX | ImGuiChildFlags_Border ) )
    {
        DrawSession();
    }
    ImGui::EndChild();
    ImGui::SameLine();
    if( ImGui::BeginChild( "##NetClients", ImVec2( 0.f, 0.f ) ) )
    {
        const std::vector<NetPlay::ChildInfo> children = NetPlay::Get().GetChildren();
        if( children.empty() )
        {
            ImGui::TextWrapped( "No child clients. Set Clients above 0 and press Play: the editor hosts a listen server and starts that many game Players that join it; "
                                "each one's Net Debugger shows here, in a tab of its own." );
        }
        else if( ImGui::BeginTabBar( "##NetClientTabs" ) )
        {
            for( const NetPlay::ChildInfo& child : children )
            {
                const std::string label = "Client " + std::to_string( child.Index ) + ( child.Running ? "" : " (exited)" ) + "###NetClient" + std::to_string( child.Index );
                if( ImGui::BeginTabItem( label.c_str() ) )
                {
                    DrawClient( child.Index, m_clients[child.Index] );
                    ImGui::EndTabItem();
                }
            }
            ImGui::EndTabBar();
        }
    }
    ImGui::EndChild();
    ImGui::End();
}


void NetDebuggerWidget::DrawSession()
{
    NetPlay& play = NetPlay::Get();
    const bool playing = m_app && m_app->IsGameRunning();
    ImGui::SeparatorText( "Play In Editor" );
    play.DrawSettings( playing );
    ImGui::SeparatorText( "Link conditions" );
    play.DrawConditions();
    ImGui::SeparatorText( "Clients" );
    if( play.GetChildren().empty() )
    {
        ImGui::TextDisabled( playing ? "Playing offline (Clients is 0)." : "None yet: they start with Play." );
    }
    else
    {
        play.DrawChildren( true );
    }
    if( !play.GetLastError().empty() )
    {
        ImGui::TextWrapped( "Last error: %s", play.GetLastError().c_str() );
    }
    DrawServer();
}


void NetDebuggerWidget::DrawServer()
{
    NetPlay& play = NetPlay::Get();
    ImGui::SeparatorText( "Listen server" );
    NetCore* net = GetEngine().Net;
    NetServer* server = net && play.IsHosting() ? net->GetServer() : nullptr;
    if( !server )
    {
        ImGui::TextDisabled( "Not hosting." );
        return;
    }
    ImGui::Text( "127.0.0.1:%u, tick %u, %zu connected", play.GetPort(), server->GetTick(), server->GetClientCount() );
    ImGui::Text( "Replay ring: %s", net->IsRecording() ? "kept (killcams are cut from it)" : "off" );
    bool lagOverlays = net->GetLagCompensation().DrawOverlays;
    if( play.DrivenCheckbox( "Lag-compensation overlays", &lagOverlays ) )
    {
        net->GetLagCompensation().DrawOverlays = lagOverlays;
    }
    ImGui::SetItemTooltip( "In the Game View: each shot's ray, the hitboxes rewound to what its shooter saw (yellow) and where they are now (grey), for 2 s" );
    const NetCore::DebugStats& debug = net->GetDebugStats();
    ImGui::TextWrapped( "Net Debugger traffic: %llu shot results in %llu bundles (%llu dropped), %llu bandwidth reports",
        static_cast<unsigned long long>( debug.ShotResults ), static_cast<unsigned long long>( debug.ShotBundles ), static_cast<unsigned long long>( debug.ShotResultsDropped ),
        static_cast<unsigned long long>( debug.BandwidthReports ) );

    // A row per connection, at the server's end.
    if( ImGui::BeginTable( "##NetConnections", 6, ImGuiTableFlags_Borders | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg ) )
    {
        ImGui::TableSetupColumn( "client" );
        ImGui::TableSetupColumn( "RTT" );
        ImGui::TableSetupColumn( "loss" );
        ImGui::TableSetupColumn( "kbps in / out" );
        ImGui::TableSetupColumn( "slack" );
        ImGui::TableSetupColumn( "missed" );
        ImGui::TableHeadersRow();
        const std::vector<NetPlay::ChildInfo> children = play.GetChildren();
        for( const auto& client : server->GetClients() )
        {
            if( !client->Connection )
            {
                continue;
            }
            int index = 0;
            for( const NetPlay::ChildInfo& child : children )
            {
                index = child.ClientId == client->Id && child.Running ? child.Index : index;
            }
            const NetStats& stats = client->Connection->GetStats();
            const auto state = index > 0 ? m_clients.find( index ) : m_clients.end();
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            if( index > 0 )
            {
                ImGui::Text( "#%u (client %d)", client->Id, index );
            }
            else
            {
                ImGui::Text( "#%u", client->Id );
            }
            ImGui::TableNextColumn();
            ImGui::Text( "%.0f ms", stats.RoundTripMs );
            ImGui::TableNextColumn();
            ImGui::Text( "%.1f%%", stats.PacketLoss * 100.f );
            ImGui::TableNextColumn();
            if( state != m_clients.end() )
            {
                ImGui::Text( "%.0f / %.0f", state->second.ServerRate.KbpsIn, state->second.ServerRate.KbpsOut );
            }
            else
            {
                ImGui::TextUnformatted( "-" );
            }
            ImGui::TableNextColumn();
            ImGui::Text( "%.2f", client->Commands.GetSlack() );
            ImGui::TableNextColumn();
            ImGui::Text( "%llu", static_cast<unsigned long long>( client->Commands.GetMissedCount() ) );
            // The blobs the server is sending it (a killcam, shot results) or taking from it.
            for( const NetBlobProgress& blob : client->Connection->GetBlobProgress() )
            {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TableNextColumn();
                char label[128];
                std::snprintf( label, sizeof( label ), "%s #%u: %.1f / %.1f KB%s", blob.Outgoing ? "out" : "in", blob.Id, static_cast<double>( blob.DoneBytes ) / 1024.0,
                    static_cast<double>( blob.Size ) / 1024.0, blob.Outgoing && !blob.Started ? " (queued)" : "" );
                ImGui::ProgressBar( blob.Size > 0 ? static_cast<float>( static_cast<double>( blob.DoneBytes ) / static_cast<double>( blob.Size ) ) : 0.f, ImVec2( -1.f, 0.f ), label );
            }
        }
        ImGui::EndTable();
    }

    // The session's top-bandwidth entities: counted while a client's debugger asks (this panel's do).
    NetReplicationServer* replication = net->GetReplicationServer();
    if( replication && replication->GetAccountedBytes() > 0 && ImGui::TreeNodeEx( "Top bandwidth (every client, this session)", ImGuiTreeNodeFlags_DefaultOpen ) )
    {
        const double seconds = m_hostingSince >= 0.0 ? std::max( 1.0, Now() - m_hostingSince ) : 1.0;
        const std::vector<NetEntityBytes> top = net->GetTopEntities( NetDebug::kTopEntities, seconds );
        const double total = static_cast<double>( replication->GetAccountedBytes() );
        for( const NetEntityBytes& entity : top )
        {
            char label[160];
            std::snprintf( label, sizeof( label ), "#%u %s: %.1f KB", entity.NetId, entity.Name.empty() ? "-" : entity.Name.c_str(), static_cast<double>( entity.Bits ) / 8.0 / 1024.0 );
            ImGui::ProgressBar( total > 0.0 ? static_cast<float>( static_cast<double>( entity.Bits ) / 8.0 / total ) : 0.f, ImVec2( -1.f, 0.f ), label );
        }
        ImGui::TextDisabled( "%.1f KB of snapshots counted, %.0f%% headers", total / 1024.0,
            total > 0.0 ? 100.0 * static_cast<double>( replication->GetOverheadBits() ) / ( total * 8.0 ) : 0.0 );
        ImGui::TreePop();
    }
}


void NetDebuggerWidget::DrawClient( int InIndex, ClientState& InState )
{
    NetPlay& play = NetPlay::Get();
    ImGui::SeparatorText( "Its own Net Debugger" );
    if( !InState.Error.empty() )
    {
        ImGui::TextColored( ImVec4( 1.f, 0.6f, 0.3f, 1.f ), "Client %d: %s", InIndex, InState.Error.c_str() );
    }
    if( !InState.HasView )
    {
        ImGui::TextDisabled( "Waiting for its first answer (net view)..." );
        DrawServerEnd( InState );
        return;
    }
    if( InState.ReceivedAt >= 0.0 && Now() - InState.ReceivedAt > 1.0 )
    {
        ImGui::TextDisabled( "(%.0f s old)", Now() - InState.ReceivedAt );
    }
    ImGui::PushID( InIndex );
    const NetDebuggerActions actions = DrawNetDebuggerView( InState.View, InState.UI, &InState.ClientHistory );
    ImGui::PopID();
    // Export, from the button or the edscript (net-ui-click Export 1): the child writes the record.
    std::string queued;
    const bool exportQueued = InState.ExportId == 0 && InState.View.HasDetail && play.TakeUi( "Export " + std::to_string( InIndex ), queued );
    if( ( actions.Export || exportQueued ) && InState.ExportId == 0 )
    {
        InState.ExportTick = actions.Export ? actions.ExportTick : InState.View.Detail.Tick;
        InState.ExportId = play.SendControl( InIndex, "net export " + std::to_string( InState.ExportTick ) );
        InState.UI.LastExport = InState.ExportId != 0 ? "exporting..." : "client " + std::to_string( InIndex ) + " isn't taking commands";
    }
    // Sent by the next Poll (after any toggle still on its way): the child ends at the newest value.
    if( actions.OverlaysChanged )
    {
        InState.OverlaysPending = true;
    }
    DrawServerEnd( InState );
}


void NetDebuggerWidget::DrawServerEnd( ClientState& InState )
{
    if( !InState.ServerHistory.Samples.empty() && ImGui::CollapsingHeader( "The server's end of its link", ImGuiTreeNodeFlags_DefaultOpen ) )
    {
        ImGui::PushID( "Server" );
        DrawNetDebuggerGraphs( InState.ServerHistory, false );
        ImGui::PopID();
    }
}

#endif
