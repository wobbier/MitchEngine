#include "PCH.h"
#include "Engine/NetLaunch.h"
#include "CLog.h"
#include "Core/CommandLine.h"
#include "Cores/NetCore.h"
#include "JSON.h"
#include "Net/NetCommands.h"
#include "Net/NetTransport.h"
#include "Net/NetVersion.h"
#include "Net/UdpTransport.h"
#include "Engine/Engine.h"
#include "Engine/Input.h"
#include "Engine/Project.h"
#include "Engine/ProjectSettings.h"
#include "VFS/VFS.h"
#include "World/Scene.h"
#include "Window/IWindow.h"
#if USING( ME_SCRIPTING )
#include "Scripting/ScriptNet.h"
#endif
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace
{
    constexpr uint16_t kDefaultPort = 7777;

    // Tools wait on these lines (Tools/RunNetSmoke.py): out now, not when the buffer fills.
    void Say( CLog::LogType InType, const std::string& InMessage )
    {
        CLog::Log( InType, InMessage );
        std::cout.flush();
    }


    // The commit checked out in InFolder or the nearest repository above it (a submodule's .git
    // file points at its git dir): "" when there's none (a shipped build).
    std::string ReadGitCommit( const std::string& InFolder )
    {
        namespace fs = std::filesystem;
        std::error_code error;
        for( fs::path folder = fs::absolute( InFolder, error ); !folder.empty(); folder = folder.parent_path() )
        {
            fs::path git = folder / ".git";
            if( fs::is_regular_file( git, error ) )
            {
                std::ifstream link( git );
                std::string line;
                std::getline( link, line );
                if( line.rfind( "gitdir: ", 0 ) != 0 )
                {
                    return {};
                }
                git = fs::path( line.substr( 8 ) );
                if( git.is_relative() )
                {
                    git = folder / git;
                }
            }
            if( fs::is_directory( git, error ) )
            {
                std::ifstream headFile( git / "HEAD" );
                std::string head;
                std::getline( headFile, head );
                if( head.rfind( "ref: ", 0 ) != 0 )
                {
                    return head;
                }
                const std::string ref = head.substr( 5 );
                // Worktrees keep refs in the common dir.
                fs::path common = git;
                if( std::ifstream commonFile( git / "commondir" ); commonFile )
                {
                    std::string relative;
                    std::getline( commonFile, relative );
                    common = fs::path( relative ).is_relative() ? git / relative : fs::path( relative );
                }
                for( const fs::path& dir : { git, common } )
                {
                    std::ifstream refFile( dir / ref );
                    std::string commit;
                    if( refFile && std::getline( refFile, commit ) && !commit.empty() )
                    {
                        return commit;
                    }
                }
                std::ifstream packed( common / "packed-refs" );
                std::string line;
                while( std::getline( packed, line ) )
                {
                    if( line.size() > 41 && line.compare( 41, std::string::npos, ref ) == 0 )
                    {
                        return line.substr( 0, 40 );
                    }
                }
                return {};
            }
            if( folder == folder.parent_path() )
            {
                break;
            }
        }
        return {};
    }

    std::string UtcNow( const char* InFormat )
    {
        const std::time_t now = std::time( nullptr );
        std::tm utc{};
#if USING( ME_PLATFORM_WINDOWS )
        gmtime_s( &utc, &now );
#else
        gmtime_r( &now, &utc );
#endif
        char text[32] = {};
        std::strftime( text, sizeof( text ), InFormat, &utc );
        return text;
    }

    std::string QuotedCommandLine()
    {
        std::string line;
        for( const std::string& arg : CommandLine::GetArgs() )
        {
            if( !line.empty() )
            {
                line += ' ';
            }
            line += arg.find_first_of( " \t\"'" ) == std::string::npos && !arg.empty() ? arg : "'" + arg + "'";
        }
        return line;
    }

    // "0.05" or "5%".
    float ParseFraction( std::string InText )
    {
        float scale = 1.f;
        if( !InText.empty() && InText.back() == '%' )
        {
            InText.pop_back();
            scale = 0.01f;
        }
        return std::clamp( static_cast<float>( std::atof( InText.c_str() ) ) * scale, 0.f, 1.f );
    }
}


NetLaunch::NetLaunch() = default;


NetLaunch::~NetLaunch() = default;


bool NetLaunch::IsRequested()
{
    return IsHosting() || CommandLine::Has( "--connect" );
}


bool NetLaunch::IsHosting()
{
#if USING( ME_HEADLESS )
    // A dedicated server has nothing else to do: a shipped one (Build Game's Server profile) hosts
    // on the default port when started with no arguments.
    return !CommandLine::Has( "--connect" );
#else
    return CommandLine::Has( "--server" );
#endif
}


void NetLaunch::ShowRole( const std::string& InRole )
{
    if( m_window && !m_title.empty() )
    {
        m_window->SetTitle( m_title + " [" + InRole + "]" );
    }
}


bool NetLaunch::Start( NetCore& InNet, uint16_t InTickRate, IWindow* InWindow, const std::string& InTitle )
{
    m_window = InWindow;
    m_title = InTitle;
    m_requested = IsRequested();
    m_reportPath = CommandLine::GetString( "--net-report" );
    m_exitWhenEmpty = CommandLine::Has( "--net-exit-when-empty" );
    if( !m_requested )
    {
        return true;
    }
    const bool host = IsHosting();
    if( host && CommandLine::Has( "--connect" ) )
    {
        m_error = "--server and --connect: pick one";
        Say( CLog::LogType::Error, "Net: " + m_error );
        return false;
    }
    if( CommandLine::Has( "--net-timeout" ) )
    {
        InNet.SetTimeout( std::max( 0.1f, CommandLine::GetFloat( "--net-timeout", 10.f ) ) );
    }
    // C# state, prediction and RPCs (NET-6), and the command layout both sides must share.
    InNet.SetCommandSchema( m_commandSchema );
#if USING( ME_SCRIPTING )
    InNet.SetScriptGlue( MakeScriptNetGlue() );
#endif

    NetAddress server = 0;
    uint16_t port = 0;
    if( host )
    {
        const std::string portText = CommandLine::GetString( "--server" );
        const int requested = portText.empty() ? kDefaultPort : std::atoi( portText.c_str() );
        if( requested < 0 || requested > 65535 || ( requested == 0 && portText != "0" ) )
        {
            m_error = "--server " + portText + ": not a port";
            Say( CLog::LogType::Error, "Net: " + m_error );
            return false;
        }
        port = static_cast<uint16_t>( requested );
    }
    else
    {
        const std::string target = CommandLine::GetString( "--connect" );
        server = UdpTransport::Resolve( target );
        if( server == 0 )
        {
            m_error = "--connect " + target + ": expected host:port (IPv4 or a name that resolves to one)";
            Say( CLog::LogType::Error, "Net: " + m_error );
            return false;
        }
    }

    m_socket = std::make_unique<UdpTransport>( port );
    if( !m_socket->IsOpen() )
    {
        m_error = "the UDP socket didn't open: " + m_socket->GetError();
        Say( CLog::LogType::Error, "Net: " + m_error );
        return false;
    }

    // Conditions apply to what this side sends.
    NetConditions conditions;
    conditions.LatencyMs = std::max( 0.f, CommandLine::GetFloat( "--net-lag", 0.f ) );
    conditions.JitterMs = std::max( 0.f, CommandLine::GetFloat( "--net-jitter", 0.f ) );
    conditions.Loss = ParseFraction( CommandLine::GetString( "--net-loss", "0" ) );
    conditions.Seed = static_cast<uint64_t>( std::max( 1, CommandLine::GetInt( "--net-seed", 1 ) ) );
    INetTransport* transport = m_socket.get();
    std::string conditioned;
    if( conditions.LatencyMs > 0.f || conditions.JitterMs > 0.f || conditions.Loss > 0.f )
    {
        m_conditioned = std::make_unique<ConditionedTransport>( *m_socket, conditions );
        transport = m_conditioned.get();
        conditioned = " (sending with " + std::to_string( static_cast<int>( conditions.LatencyMs ) ) + "+-" + std::to_string( static_cast<int>( conditions.JitterMs ) )
            + " ms, " + std::to_string( static_cast<int>( conditions.Loss * 100.f + 0.5f ) ) + "% loss)";
    }

    if( host )
    {
        InNet.Host( *transport, InTickRate );
        Say( CLog::LogType::Info, "Net: hosting on UDP port " + std::to_string( m_socket->GetPort() ) + " at " + std::to_string( InTickRate ) + " Hz" + conditioned );
        ShowRole( "server :" + std::to_string( m_socket->GetPort() ) );
        StartRecording( InNet );
    }
    else
    {
        if( CommandLine::Has( "--record-replay" ) )
        {
            Say( CLog::LogType::Warning, "Net: --record-replay records on the server; a client doesn't record yet" );
        }
        InNet.Connect( *transport, server );
        Say( CLog::LogType::Info, "Net: connecting to " + UdpTransport::ToString( server ) + conditioned );
        ShowRole( "connecting to " + UdpTransport::ToString( server ) );
    }
    return true;
}


bool NetLaunch::Update( NetCore& InNet )
{
    if( !m_requested )
    {
        return false;
    }
    if( !m_error.empty() )
    {
        return true;
    }
    if( NetServer* server = InNet.GetServer() )
    {
        if( server->GetAcceptedCount() != m_lastAccepted )
        {
            Say( CLog::LogType::Info, "Net: client joined (" + std::to_string( server->GetClientCount() ) + " connected)" );
        }
        if( server->GetDisconnectCount() != m_lastDisconnects )
        {
            Say( CLog::LogType::Info, "Net: client left (" + std::to_string( server->GetClientCount() ) + " connected)" );
        }
        if( server->GetTimeoutCount() != m_lastTimeouts )
        {
            Say( CLog::LogType::Warning, "Net: client timed out (" + std::to_string( server->GetClientCount() ) + " connected)" );
        }
        if( server->GetRejectedCount() != m_lastRejected )
        {
            Say( CLog::LogType::Warning, "Net: turned a client away: " + server->GetLastRejection() );
        }
        m_lastAccepted = server->GetAcceptedCount();
        m_lastDisconnects = server->GetDisconnectCount();
        m_lastTimeouts = server->GetTimeoutCount();
        m_lastRejected = server->GetRejectedCount();
        if( m_exitWhenEmpty && m_lastAccepted > 0 && server->GetClientCount() == 0 )
        {
            Say( CLog::LogType::Info, "Net: the last client has gone; exiting (--net-exit-when-empty)" );
            return true;
        }
    }
    else if( NetClient* client = InNet.GetClient() )
    {
        const int state = static_cast<int>( client->GetState() );
        if( state != m_lastState )
        {
            m_lastState = state;
            switch( client->GetState() )
            {
            case NetClient::State::Connected:
                Say( CLog::LogType::Info, "Net: connected as client #" + std::to_string( client->GetClientId() ) + " at " + std::to_string( client->GetTickRate() ) + " Hz" );
                ShowRole( "client #" + std::to_string( client->GetClientId() ) );
                break;
            case NetClient::State::Rejected:
                Say( CLog::LogType::Error, "Net: the server turned us away: " + client->GetRejectReason() );
                ShowRole( "rejected" );
                break;
            case NetClient::State::TimedOut:
                Say( CLog::LogType::Error, "Net: the server stopped answering (timed out)" );
                ShowRole( "timed out" );
                break;
            case NetClient::State::Disconnected:
                Say( CLog::LogType::Warning, "Net: disconnected: " + client->GetDisconnectReason() );
                ShowRole( "disconnected" );
                break;
            default:
                break;
            }
        }
    }
    return false;
}


void NetLaunch::BuildCommandLayout( const Input& InInput )
{
    m_commandSlots.clear();
    m_overrides = 0;
    ++m_layoutVersion;
    m_layoutMapVersion = const_cast<Input&>( InInput ).GetActions().GetMapVersion();
    std::vector<std::pair<std::string, InputActionType>> actions;
    for( const InputContext& context : const_cast<Input&>( InInput ).GetActions().GetMap().Contexts )
    {
        for( const InputAction& action : context.Actions )
        {
            const bool known = std::any_of( actions.begin(), actions.end(), [&action]( const auto& InKnown ) { return InKnown.first == action.Name; } );
            if( action.Networked && !known )
            {
                actions.emplace_back( action.Name, action.Type );
            }
        }
    }
    std::sort( actions.begin(), actions.end() );
    int bit = 0;
    int axis = 0;
    uint64_t schema = NetVersion::Hash( "commands" );
    for( const auto& [name, type] : actions )
    {
        CommandSlot slot;
        slot.Action = name;
        slot.IsButton = type == InputActionType::Button;
        slot.AxisCount = type == InputActionType::Vector2 ? 2 : 1;
        if( slot.IsButton ? bit >= 32 : axis + slot.AxisCount > 4 )
        {
            Say( CLog::LogType::Warning, "Net: no room in the command for the networked action " + name + " (32 buttons, 4 axes)" );
            continue;
        }
        slot.Index = slot.IsButton ? bit++ : axis;
        if( !slot.IsButton )
        {
            axis += slot.AxisCount;
        }
        schema = NetVersion::Hash( name + ( slot.IsButton ? ":b\n" : ":a" + std::to_string( slot.AxisCount ) + "\n" ), schema );
        m_commandSlots.push_back( slot );
    }
    m_commandSchema = schema;
}


void NetLaunch::RefreshCommandLayout( const Input& InInput )
{
    if( const_cast<Input&>( InInput ).GetActions().GetMapVersion() != m_layoutMapVersion )
    {
        BuildCommandLayout( InInput );
    }
}


void NetLaunch::SampleCommand( const Input& InInput, InputCommand& OutCommand ) const
{
    OutCommand.Buttons = m_overrides;
    OutCommand.Axes = {};
    // Quantized here, so the client predicts with exactly the aim the server will read.
    OutCommand.HasAim = m_hasAim;
    OutCommand.Yaw = m_hasAim ? NetCommands::QuantizeYaw( m_yaw ) : 0.f;
    OutCommand.Pitch = m_hasAim ? NetCommands::QuantizePitch( m_pitch ) : 0.f;
    for( const CommandSlot& slot : m_commandSlots )
    {
        const InputActionState& state = InInput.GetAction( slot.Action );
        if( slot.IsButton )
        {
            // Inside a fixed step WasPressed carries the edges since the last step: a tap shorter
            // than a tick still holds the bit for one tick.
            OutCommand.Buttons |= state.IsPressed() || state.WasPressed() ? ( 1u << slot.Index ) : 0u;
        }
        else if( slot.AxisCount == 2 )
        {
            OutCommand.Axes[slot.Index] = std::clamp( state.Value.x, -1.f, 1.f );
            OutCommand.Axes[slot.Index + 1] = std::clamp( state.Value.y, -1.f, 1.f );
        }
        else
        {
            OutCommand.Axes[slot.Index] = std::clamp( state.Value.x, -1.f, 1.f );
        }
    }
}


int NetLaunch::GetActionIndex( const std::string& InAction ) const
{
    for( const CommandSlot& slot : m_commandSlots )
    {
        if( slot.Action == InAction && slot.IsButton )
        {
            return slot.Index;
        }
    }
    return -1;
}


int NetLaunch::GetAxisIndex( const std::string& InAction, int* OutCount ) const
{
    for( const CommandSlot& slot : m_commandSlots )
    {
        if( slot.Action == InAction && !slot.IsButton )
        {
            if( OutCount )
            {
                *OutCount = slot.AxisCount;
            }
            return slot.Index;
        }
    }
    if( OutCount )
    {
        *OutCount = 0;
    }
    return -1;
}


void NetLaunch::RecordLocalCommand( const InputCommand& InCommand )
{
    m_previousLocalCommand = m_localCommand;
    m_localCommand = InCommand;
    m_localCommand.Tick = m_localTick++;
    // As the server would read it: offline play behaves like a session.
    for( float& axis : m_localCommand.Axes )
    {
        axis = NetCommands::QuantizeAxis( axis );
    }
    m_hasLocalCommand = true;
}


void NetLaunch::ClearLocalCommand()
{
    m_localCommand = InputCommand();
    m_previousLocalCommand = InputCommand();
    m_hasLocalCommand = false;
}


void NetLaunch::ResetLocalInput()
{
    m_overrides = 0;
    m_hasAim = false;
    m_yaw = 0.f;
    m_pitch = 0.f;
    ClearLocalCommand();
    m_localTick = 0;
}


void NetLaunch::SetActionOverride( int InIndex, bool InHeld )
{
    if( InIndex < 0 || InIndex >= 32 )
    {
        return;
    }
    const uint32_t bit = 1u << InIndex;
    m_overrides = InHeld ? ( m_overrides | bit ) : ( m_overrides & ~bit );
}


void NetLaunch::SetAim( float InYaw, float InPitch )
{
    if( !std::isfinite( InYaw ) || !std::isfinite( InPitch ) )
    {
        return;
    }
    m_hasAim = true;
    m_yaw = InYaw;
    m_pitch = std::clamp( InPitch, -90.f, 90.f );
}


void NetLaunch::StartRecording( NetCore& InNet )
{
    if( !CommandLine::Has( "--record-replay" ) )
    {
        return;
    }
    // The file: user:// goes to the user folder; a name without a folder too, under Replays/; any
    // other relative path is against the working directory.
    std::string path = CommandLine::GetString( "--record-replay" );
    if( path.empty() )
    {
        path = std::string( VFS::kUserScheme ) + "Replays/" + UtcNow( "%Y%m%d-%H%M%S" ) + ".mreplay";
    }
    else if( path.rfind( VFS::kUserScheme, 0 ) != 0 && !std::filesystem::path( path ).has_parent_path() )
    {
        // "match.mreplay": where the default ones go, not wherever the process was started.
        path = std::string( VFS::kUserScheme ) + "Replays/" + path;
    }
    if( path.rfind( VFS::kUserScheme, 0 ) == 0 )
    {
        path = VFS::Get().GetUserRoot() + "/" + path.substr( std::string( VFS::kUserScheme ).size() );
    }
    std::error_code error;
    path = std::filesystem::absolute( path, error ).lexically_normal().generic_string();

    NetReplayHeader header;
    const ProjectSettings& settings = ProjectSettings::Get();
    header.Product = settings.ProductName + " " + settings.ProductVersion;
    header.EngineCommit = ReadGitCommit( Project::GetEngineRoot() );
    header.GameCommit = ReadGitCommit( Project::GetRoot() );
    header.Map = GetEngine().CurrentScene ? GetEngine().CurrentScene->FilePath.GetLocalPathString() : std::string();
    header.CommandLine = QuotedCommandLine();
    header.RecordedAt = UtcNow( "%Y-%m-%dT%H:%M:%SZ" );
    if( InNet.StartRecording( path, header ) )
    {
        Say( CLog::LogType::Info, "Net: recording the match replay to " + path );
    }
    else
    {
        Say( CLog::LogType::Error, "Net: can't record the match replay to " + path );
    }
}


void NetLaunch::Shutdown( NetCore& InNet, double InNow )
{
    if( !m_requested )
    {
        return;
    }
    // The replay ends with the match: its last chunk and index are written before the report
    // (which counts them) and the goodbyes.
    InNet.StopRecording();
    // The report first: it describes the session as it ran, before the goodbyes.
    if( !m_reportPath.empty() )
    {
        if( m_error.empty() )
        {
            InNet.WriteReport( m_reportPath, InNow );
        }
        else
        {
            json report;
            report["Role"] = "none";
            report["Error"] = m_error;
            std::ofstream out( m_reportPath );
            out << report.dump( 4 ) << std::endl;
        }
    }
    InNet.Close( InNow );
    InNet.Shutdown();
    m_conditioned.reset();
    m_socket.reset();
}
