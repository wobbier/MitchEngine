#include "PCH.h"
#include "Engine/NetLaunch.h"
#include "CLog.h"
#include "Core/CommandLine.h"
#include "Cores/NetCore.h"
#include "JSON.h"
#include "Net/NetCommands.h"
#include "Net/NetTransport.h"
#include "Net/NetReplayDump.h"
#include "Net/NetVersion.h"
#include "Net/UdpTransport.h"
#include "Components/Networking/NetIdentity.h"
#include "Components/Networking/SingletonInput.h"
#include "Components/Transform.h"
#include "Engine/DevConsole.h"
#include "Engine/Engine.h"
#include "Engine/Input.h"
#include "Engine/Project.h"
#include "Engine/ProjectSettings.h"
#include "Engine/ReplayViewer.h"
#include "VFS/VFS.h"
#include "World/Scene.h"
#include "Window/IWindow.h"
#if USING( ME_SCRIPTING )
#include "Scripting/ScriptNet.h"
#endif
#if USING( ME_IMGUI ) && !USING( ME_EDITOR )
#include <imgui.h>
#endif
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>

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
    return IsHosting() || CommandLine::Has( "--connect" ) || IsReplaying();
}


bool NetLaunch::IsReplaying()
{
    // --replay-dump FILE --resim-check needs the world: it plays FILE like --replay (NetReplayDump
    // leaves it to the engine).
    return CommandLine::Has( "--replay" ) || ( CommandLine::Has( "--replay-dump" ) && CommandLine::Has( "--resim-check" ) );
}


int NetLaunch::GetExitCode() const
{
    if( !m_error.empty() )
    {
        return 1;
    }
    const int viewer = m_viewer ? m_viewer->GetExitCode() : m_viewerExitCode;
    return viewer >= 0 ? viewer : 0;
}


bool NetLaunch::IsHosting()
{
#if USING( ME_HEADLESS )
    // A dedicated server has nothing else to do: a shipped one (Build Game's Server profile) hosts
    // on the default port when started with no arguments.
    return !CommandLine::Has( "--connect" ) && !IsReplaying();
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
    m_netDebugger.Open = CommandLine::Has( "--net-debugger" );
    RegisterNetCommands( InNet );
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
    InNet.SetCommandSchema( InNet.GetWorld().Singleton<SingletonInput>().GetCommandSchema() );
#if USING( ME_SCRIPTING )
    InNet.SetScriptGlue( MakeScriptNetGlue() );
#endif
    if( IsReplaying() )
    {
        if( host || CommandLine::Has( "--connect" ) )
        {
            m_error = "--replay plays without a session: drop --server / --connect";
            Say( CLog::LogType::Error, "Net: " + m_error );
            return false;
        }
        return StartReplay( InNet );
    }

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
    if( NetReplayPlayback* playback = InNet.GetPlayback() )
    {
        // The viewer's tools: the timeline, a marker jump, the resim check, the overlays.
        if( m_viewer )
        {
            std::string error;
            const bool done = m_viewer->Update( InNet, error );
            if( !error.empty() )
            {
                m_error = error;
                Say( CLog::LogType::Error, "Replay: " + m_error );
                return true;
            }
            if( done )
            {
                return true;
            }
        }
        // --replay-check: once the playhead rests on its tick (a marker jump done, and a few frames
        // have let what it spawned settle), check and quit.
        if( m_replayCheck && !m_replayChecked && playback->IsPaused() && playback->GetPauseAt() < 0 && !( m_viewer && m_viewer->IsSettling() ) )
        {
            if( ++m_replayCheckWait >= 3 )
            {
                m_replayChecked = true;
                if( RunReplayCheck( InNet ) && m_viewer )
                {
                    std::string error;
                    if( !m_viewer->OnChecked( InNet, error ) )
                    {
                        m_error = error;
                        Say( CLog::LogType::Error, "Replay: " + m_error );
                    }
                }
                return true;
            }
        }
        else
        {
            m_replayCheckWait = 0;
        }
        return false;
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


void NetLaunch::StartRecording( NetCore& InNet )
{
    NetReplayHeader header;
    const ProjectSettings& settings = ProjectSettings::Get();
    header.Product = settings.ProductName + " " + settings.ProductVersion;
    header.EngineCommit = ReadGitCommit( Project::GetEngineRoot() );
    header.GameCommit = ReadGitCommit( Project::GetRoot() );
    header.Map = GetEngine().CurrentScene ? GetEngine().CurrentScene->FilePath.GetLocalPathString() : std::string();
    header.CommandLine = QuotedCommandLine();
    header.RecordedAt = UtcNow( "%Y-%m-%dT%H:%M:%SZ" );
    if( !CommandLine::Has( "--record-replay" ) )
    {
        // The ring alone (ME-323): what Net.SendKillcam cuts killcams from.
        if( !CommandLine::Has( "--no-replay-ring" ) && InNet.StartRecording( "", header ) )
        {
            Say( CLog::LogType::Info, "Net: keeping a " + std::to_string( static_cast<int>( InNet.GetRecorder()->GetSettings().RingSeconds ) )
                + " s replay ring for killcams" );
        }
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
    if( InNet.StartRecording( path, header ) )
    {
        Say( CLog::LogType::Info, "Net: recording the match replay to " + path );
    }
    else
    {
        Say( CLog::LogType::Error, "Net: can't record the match replay to " + path );
    }
}


namespace
{
    // A replay path: user:// to the user folder; a bare name that isn't here is looked for under
    // user://Replays/ (where --record-replay puts them).
    std::string ResolveReplayPath( std::string InPath )
    {
        namespace fs = std::filesystem;
        std::error_code error;
        if( InPath.rfind( VFS::kUserScheme, 0 ) == 0 )
        {
            InPath = VFS::Get().GetUserRoot() + "/" + InPath.substr( std::string( VFS::kUserScheme ).size() );
        }
        else if( !fs::exists( InPath, error ) && !fs::path( InPath ).has_parent_path() )
        {
            const std::string replays = VFS::Get().GetUserRoot() + "/Replays/" + InPath;
            if( fs::exists( replays, error ) )
            {
                InPath = replays;
            }
        }
        return fs::absolute( InPath, error ).lexically_normal().generic_string();
    }

    std::string Status( const NetReplayPlayback& InPlayback )
    {
        char speed[16];
        std::snprintf( speed, sizeof( speed ), "%.2gx", InPlayback.GetSpeed() );
        std::string text = "tick " + std::to_string( InPlayback.GetTick() ) + " (" + std::to_string( InPlayback.GetFirstTick() ) + "-"
            + std::to_string( InPlayback.GetLastTick() ) + "), " + ( InPlayback.IsPaused() ? "paused" : "playing" ) + " at " + speed;
        if( InPlayback.IsLooping() )
        {
            text += ", looping " + std::to_string( InPlayback.GetLoopFrom() ) + "-" + std::to_string( InPlayback.GetLoopTo() );
        }
        if( InPlayback.GetPauseAt() >= 0 )
        {
            text += ", pausing at " + std::to_string( InPlayback.GetPauseAt() );
        }
        return text;
    }

    // "A:B" -> the two ticks; false when it isn't that.
    bool ParseRange( const std::string& InText, uint32_t& OutFrom, uint32_t& OutTo )
    {
        const size_t colon = InText.find( ':' );
        if( colon == std::string::npos || colon == 0 || colon + 1 >= InText.size() )
        {
            return false;
        }
        char* end = nullptr;
        const unsigned long from = std::strtoul( InText.c_str(), &end, 10 );
        if( end != InText.c_str() + colon )
        {
            return false;
        }
        const unsigned long to = std::strtoul( InText.c_str() + colon + 1, &end, 10 );
        if( *end != '\0' )
        {
            return false;
        }
        OutFrom = static_cast<uint32_t>( from );
        OutTo = static_cast<uint32_t>( to );
        return true;
    }

    // A tick on the command line: digits only (atoi would read "abc" or "-1" as a tick).
    bool ParseTick( const std::string& InText, uint32_t& OutTick )
    {
        if( InText.empty() || InText.find_first_not_of( "0123456789" ) != std::string::npos )
        {
            return false;
        }
        const unsigned long long tick = std::strtoull( InText.c_str(), nullptr, 10 );
        if( tick > std::numeric_limits<uint32_t>::max() )
        {
            return false;
        }
        OutTick = static_cast<uint32_t>( tick );
        return true;
    }
}


bool NetLaunch::StartReplay( NetCore& InNet )
{
    const std::string path = ResolveReplayPath( CommandLine::GetString( CommandLine::Has( "--replay" ) ? "--replay" : "--replay-dump" ) );
    if( CommandLine::Has( "--resim-check" ) && CommandLine::Has( "--replay-check" ) )
    {
        m_error = "--resim-check and --replay-check: one at a time";
        Say( CLog::LogType::Error, "Net: " + m_error );
        return false;
    }
    auto source = std::make_unique<NetReplayFileSource>();
    if( !source->Open( path ) )
    {
        m_error = "--replay " + path + ": " + source->GetError();
        Say( CLog::LogType::Error, "Net: " + m_error );
        return false;
    }
    const NetReplayHeader header = source->GetHeader();
    const bool indexed = source->HasIndex();
    std::string error;
    if( !InNet.StartPlayback( std::move( source ), &error ) )
    {
        m_error = "--replay " + path + ": " + error;
        Say( CLog::LogType::Error, "Net: " + m_error );
        return false;
    }
    NetReplayPlayback& playback = *InNet.GetPlayback();
    const std::string scene = GetEngine().CurrentScene ? GetEngine().CurrentScene->FilePath.GetLocalPathString() : std::string();
    if( !header.Map.empty() && !scene.empty() && header.Map != scene )
    {
        Say( CLog::LogType::Warning, "Replay: recorded on " + header.Map + ", playing in " + scene + " (their networked entities match)" );
    }
    Say( CLog::LogType::Info, "Replay: playing " + path + ": ticks " + std::to_string( playback.GetFirstTick() ) + "-" + std::to_string( playback.GetLastTick() ) + " at "
        + std::to_string( playback.GetTickRate() ) + " Hz, recorded " + header.RecordedAt + " on " + ( header.Map.empty() ? std::string( "?" ) : header.Map )
        + ( indexed ? "" : " (no index: its server never closed it)" ) );

    if( CommandLine::Has( "--replay-speed" ) )
    {
        playback.SetSpeed( CommandLine::GetFloat( "--replay-speed", 1.f ) );
    }
    if( CommandLine::Has( "--replay-loop" ) )
    {
        uint32_t from = 0;
        uint32_t to = 0;
        if( !ParseRange( CommandLine::GetString( "--replay-loop" ), from, to ) )
        {
            m_error = "--replay-loop " + CommandLine::GetString( "--replay-loop" ) + ": expected A:B (ticks)";
            Say( CLog::LogType::Error, "Net: " + m_error );
            return false;
        }
        playback.SetLoop( from, to );
        playback.Seek( playback.GetLoopFrom() );
    }
    if( CommandLine::Has( "--replay-start-tick" ) )
    {
        uint32_t start = 0;
        if( !ParseTick( CommandLine::GetString( "--replay-start-tick" ), start ) )
        {
            m_error = "--replay-start-tick " + CommandLine::GetString( "--replay-start-tick" ) + ": expected a tick";
            Say( CLog::LogType::Error, "Net: " + m_error );
            return false;
        }
        playback.Seek( start );
    }
    m_replayCheck = CommandLine::Has( "--replay-check" );
    if( CommandLine::Has( "--replay-pause-at" ) )
    {
        // A tick playing can reach: in the replay, and in the loop when there is one (a pause-at it
        // never reaches would leave --replay-check waiting forever).
        uint32_t pauseAt = 0;
        const std::string text = CommandLine::GetString( "--replay-pause-at" );
        const uint32_t from = playback.IsLooping() ? playback.GetLoopFrom() : playback.GetFirstTick();
        const uint32_t to = playback.IsLooping() ? playback.GetLoopTo() : playback.GetLastTick();
        if( !ParseTick( text, pauseAt ) )
        {
            m_error = "--replay-pause-at " + text + ": expected a tick";
        }
        else if( pauseAt < from || pauseAt > to )
        {
            m_error = "--replay-pause-at " + text + ": playing never reaches it (ticks " + std::to_string( from ) + "-" + std::to_string( to )
                + ( playback.IsLooping() ? ", the loop)" : ")" );
        }
        else if( m_replayCheck && CommandLine::Has( "--replay-paused" ) && pauseAt > playback.GetTick() )
        {
            m_error = "--replay-pause-at " + text + " with --replay-paused: it never plays there to check (drop one)";
        }
        if( !m_error.empty() )
        {
            Say( CLog::LogType::Error, "Net: " + m_error );
            return false;
        }
        playback.SetPauseAt( pauseAt );
        // Already there (or past it): show it, paused.
        if( pauseAt <= playback.GetTick() )
        {
            playback.Seek( static_cast<double>( pauseAt ) );
            playback.SetPauseAt( -1 );
        }
    }
    // A marker jump and the resim check move the playhead themselves (ReplayViewer).
    const bool paused = CommandLine::Has( "--replay-paused" ) || ( CommandLine::Has( "--replay-pause-at" ) && playback.GetPauseAt() < 0 )
        || CommandLine::Has( "--replay-marker" ) || CommandLine::Has( "--resim-check" );
    if( !paused )
    {
        playback.Play();
    }
    if( m_replayCheck && !paused && playback.GetPauseAt() < 0 )
    {
        Say( CLog::LogType::Warning, "Replay: --replay-check checks where it pauses: give --replay-pause-at or --replay-paused" );
    }
    // The viewer's tools, with the networked actions' names for the input view.
    std::vector<ReplayViewer::Action> actions;
    for( const SingletonInput::CommandSlot& slot : InNet.GetWorld().Singleton<SingletonInput>().GetCommandSlots() )
    {
        actions.push_back( { slot.Action, slot.Index, slot.IsButton, slot.AxisCount } );
    }
    m_viewer = std::make_unique<ReplayViewer>( path, std::move( actions ) );
    std::string viewerError;
    if( !m_viewer->Configure( InNet, viewerError ) )
    {
        m_error = viewerError;
        Say( CLog::LogType::Error, "Net: " + m_error );
        return false;
    }
    RegisterReplayCommands( InNet );
    ShowRole( CommandLine::Has( "--resim-check" ) ? "resim check" : "replay" );
    return true;
}


bool NetLaunch::RunReplayCheck( NetCore& InNet )
{
    const NetReplayPlayback::Check check = InNet.CheckPlayback();
    const NetReplayPlayback* playback = InNet.GetPlayback();
    // Every recorded entity where the world has it (tools read these lines).
    if( playback && playback->GetRecord() )
    {
        for( const auto& [id, state] : playback->GetRecord()->State.Entities )
        {
            auto entity = playback->GetEntityTable().find( id );
            const std::string name = entity != playback->GetEntityTable().end() ? entity->second.Name : std::string();
            EntityHandle handle = InNet.FindEntity( id );
            const Vector3 at = handle && handle->HasComponent<Transform>() ? handle->GetComponent<Transform>().GetWorldPosition() : state.GetPosition();
            // The world's owner (its NetIdentity), not the recording's: the check compares the two.
            const NetIdentity* identity = handle ? handle->TryGetComponent<NetIdentity>() : nullptr;
            char line[256];
            std::snprintf( line, sizeof( line ), "Replay: tick %u entity %u %s at (%.4f, %.4f, %.4f) owner %u", check.Tick, id, name.empty() ? "-" : name.c_str(), at.x, at.y, at.z,
                identity ? static_cast<unsigned>( identity->GetOwner() ) : 0u );
            Say( CLog::LogType::Info, line );
        }
    }
    for( const std::string& problem : check.Problems )
    {
        Say( CLog::LogType::Error, "Replay: check: " + problem );
    }
    if( check.Passed() )
    {
        Say( CLog::LogType::Info, "Replay: check passed at tick " + std::to_string( check.Tick ) + ": " + std::to_string( check.Matched ) + " of " + std::to_string( check.Entities )
            + " entities are the recording" );
        return true;
    }
    m_error = "replay check failed at tick " + std::to_string( check.Tick ) + ": " + std::to_string( check.Problems.size() ) + " problem(s), " + std::to_string( check.Matched ) + " of "
        + std::to_string( check.Entities ) + " entities match";
    Say( CLog::LogType::Error, "Replay: " + m_error );
    return false;
}


void NetLaunch::RegisterReplayCommands( NetCore& InNet )
{
    static bool registered = false;
    if( registered )
    {
        return;
    }
    registered = true;
    NetCore* net = &InNet;
    NetLaunch* self = this;
    DevConsole::Get().Register( "replay",
        "the replay playing (--replay): replay [play|pause|toggle|step [n]|seek <tick>|speed <x>|loop <a> <b>|loop off|pause-at <tick>|check|info"
        "|markers [kind]|marker <kind> [n]|next [kind]|prev [kind]|inspect [netid]|input|shots|resim [netid] [a:b]|camera game|free|follow <id>|pov <id>"
        "|overlay shots|hitboxes|trails on|off|report <file>]",
        [net, self]( const std::vector<std::string>& InArgs ) -> std::string {
            NetReplayPlayback* playback = net->GetPlayback();
            if( !playback )
            {
                return "no replay is playing (start the Player with --replay <file>)";
            }
            const std::string verb = InArgs.empty() ? std::string() : InArgs[0];
            auto number = [&InArgs]( size_t InIndex, double InDefault ) {
                return InArgs.size() > InIndex ? std::atof( InArgs[InIndex].c_str() ) : InDefault;
            };
            if( verb == "play" )
            {
                playback->Play();
            }
            else if( verb == "pause" )
            {
                playback->Pause();
            }
            else if( verb == "toggle" )
            {
                playback->TogglePause();
            }
            else if( verb == "step" )
            {
                playback->Step( static_cast<int64_t>( number( 1, 1.0 ) ) );
            }
            else if( verb == "seek" )
            {
                if( InArgs.size() < 2 )
                {
                    return "replay seek <tick>";
                }
                playback->Seek( number( 1, 0.0 ) );
            }
            else if( verb == "speed" )
            {
                playback->SetSpeed( number( 1, 1.0 ) );
            }
            else if( verb == "loop" )
            {
                if( InArgs.size() == 2 && InArgs[1] == "off" )
                {
                    playback->ClearLoop();
                }
                else if( InArgs.size() >= 3 )
                {
                    playback->SetLoop( static_cast<uint32_t>( number( 1, 0.0 ) ), static_cast<uint32_t>( number( 2, 0.0 ) ) );
                }
                else
                {
                    return "replay loop <from> <to> | replay loop off";
                }
            }
            else if( verb == "pause-at" )
            {
                playback->SetPauseAt( static_cast<int64_t>( number( 1, -1.0 ) ) );
            }
            else if( verb == "check" )
            {
                const NetReplayPlayback::Check check = net->CheckPlayback();
                std::string out = check.Passed() ? "check passed at tick " + std::to_string( check.Tick ) + ": " + std::to_string( check.Matched ) + " entities"
                                                 : "check FAILED at tick " + std::to_string( check.Tick );
                for( const std::string& problem : check.Problems )
                {
                    out += "\n  " + problem;
                }
                return out;
            }
            else if( verb == "info" )
            {
                const NetReplayHeader& header = playback->GetHeader();
                return header.Product + ", recorded " + header.RecordedAt + " on " + header.Map + " at " + std::to_string( header.TickRate ) + " Hz\n  engine " + header.EngineCommit
                    + ", game " + header.GameCommit + "\n  " + header.CommandLine;
            }
            else if( !verb.empty() )
            {
                bool handled = false;
                const std::string out = self->m_viewer ? self->m_viewer->RunCommand( *net, InArgs, handled ) : std::string();
                return handled ? out : "unknown: replay " + verb + " (help replay)";
            }
            return Status( *playback );
        } );
}


namespace
{
    // The client's link and prediction in one line (the console's "net").
    std::string NetStatus( NetCore& InNet )
    {
        NetClient* client = InNet.GetClient();
        if( !client || client->GetState() != NetClient::State::Connected )
        {
            return "no connected client session";
        }
        const NetStats& stats = client->GetConnection().GetStats();
        char line[512];
        std::snprintf( line, sizeof( line ), "client #%u tick %u: %.1f ms RTT, %.1f%% loss, slack %.2f (target %.2f), dilation %+.2f%%", client->GetClientId(),
            client->GetClock().GetTick(), stats.RoundTripMs, stats.PacketLoss * 100.f, client->GetServerSlack(), client->GetClock().GetTargetSlack(),
            client->GetClock().GetDilation() * 100.f );
        std::string out = line;
        // Blobs on their way (ME-323), either way.
        for( const NetBlobProgress& blob : client->GetConnection().GetBlobProgress() )
        {
            std::snprintf( line, sizeof( line ), "; blob %s #%u %zu/%zu bytes%s", blob.Outgoing ? "out" : "in", blob.Id, blob.DoneBytes, blob.Size,
                blob.Outgoing && !blob.Started ? " (queued)" : "" );
            out += line;
        }
        if( const NetPredictor* predictor = InNet.GetPredictor() )
        {
            std::snprintf( line, sizeof( line ), "; prediction: %d corrections, %d matches, %d initial sync, %zu mismatches on the timeline", predictor->GetCorrectionCount(),
                predictor->GetMatchCount(), predictor->GetInitialSyncCount(), predictor->GetMispredicts().size() );
            out += line;
        }
        return out;
    }

    // Where the Net Debugger and the console export mispredictions.
    std::string MispredictFolder()
    {
        const std::string root = Project::GetRoot();
        return ( root.empty() ? std::string( "." ) : root ) + "/.tmp/Net";
    }

    // InTick's record (0: the newest), written on the background lane: its path, or why not.
    std::string ExportMispredict( NetCore& InNet, uint32_t InTick, bool& OutExported )
    {
        OutExported = false;
        const NetPredictor* predictor = InNet.GetPredictor();
        if( !predictor || predictor->GetMispredicts().empty() )
        {
            return "no mispredictions on the timeline";
        }
        const NetMispredictRecord* record = InTick == 0 ? &predictor->GetMispredicts().back() : predictor->FindMispredict( InTick );
        if( !record )
        {
            return "no misprediction at tick " + std::to_string( InTick ) + " (net mispredicts lists them)";
        }
        const std::string path = NetMispredict::DefaultPath( MispredictFolder(), record->Tick );
        NetMispredict::Export( *record, path );     // logged if it fails
        OutExported = true;
        return path;
    }
}


void NetLaunch::RegisterNetCommands( NetCore& InNet )
{
    static bool registered = false;
    if( registered )
    {
        return;
    }
    registered = true;
    NetCore* net = &InNet;
    NetLaunch* self = this;
    DevConsole::Get().Register( "net",
        "the client session (NET-7): net [status|debugger [on|off]|mispredicts|export [tick]] (export: the newest misprediction, or the tick's, to .tmp/Net/Mispredict_<tick>.json)",
        [net, self]( const std::vector<std::string>& InArgs ) -> std::string {
            const std::string verb = InArgs.empty() ? std::string( "status" ) : InArgs[0];
            if( verb == "status" )
            {
                return NetStatus( *net );
            }
            if( verb == "debugger" )
            {
                self->m_netDebugger.Open = InArgs.size() > 1 ? InArgs[1] != "off" : !self->m_netDebugger.Open;
                return std::string( "Net Debugger " ) + ( self->m_netDebugger.Open ? "open" : "closed" );
            }
            if( verb == "mispredicts" )
            {
                const NetPredictor* predictor = net->GetPredictor();
                if( !predictor )
                {
                    return "nothing predicted";
                }
                // Counted from the timeline: a steady desync has more than the records kept.
                size_t onTimeline = 0;
                uint32_t newest = 0;
                if( predictor->GetNewestReconciled( newest ) )
                {
                    const uint32_t span = predictor->GetTimelineTicks();
                    for( uint32_t tick = newest + 1 >= span ? newest + 1 - span : 0; tick <= newest; ++tick )
                    {
                        const NetPredictOutcome outcome = predictor->GetOutcome( tick );
                        onTimeline += outcome == NetPredictOutcome::Correction || outcome == NetPredictOutcome::InitialSync ? 1 : 0;
                    }
                }
                std::string out = std::to_string( onTimeline ) + " mismatch(es) in the last " + std::to_string( static_cast<int>( NetPredictor::kTimelineSeconds ) ) + " s, "
                    + std::to_string( predictor->GetMispredicts().size() ) + " with a record (" + std::to_string( predictor->GetCorrectionCount() ) + " corrections, " + std::to_string( predictor->GetMatchCount() ) + " matches in all)";
                for( const NetMispredictRecord& record : predictor->GetMispredicts() )
                {
                    out += "\n  tick " + std::to_string( record.Tick ) + ( record.InitialSync ? " (initial sync)" : "" ) + ": " + record.Describe() + "; "
                        + std::to_string( record.Resimulated.size() ) + " ticks resimulated";
                }
                return out;
            }
            if( verb == "export" )
            {
                bool exported = false;
                const std::string out = ExportMispredict( *net, InArgs.size() > 1 ? static_cast<uint32_t>( std::strtoul( InArgs[1].c_str(), nullptr, 10 ) ) : 0u, exported );
                return exported ? "writing " + out : out;
            }
            return "unknown: net " + verb + " (help net)";
        } );
}


void NetLaunch::DrawNetDebugger( NetCore& InNet )
{
#if USING( ME_IMGUI ) && !USING( ME_EDITOR )
    NetClient* client = InNet.GetClient();
    if( !m_netDebugger.Open || !client )
    {
        return;
    }
    // The link's rate over the last second, from the connection's byte counts.
    const NetStats& stats = client->GetConnection().GetStats();
    const double now = std::chrono::duration<double>( std::chrono::steady_clock::now().time_since_epoch() ).count();
    if( m_netDebugger.RateAt < 0.0 || stats.BytesReceived < m_netDebugger.BytesIn )
    {
        m_netDebugger.RateAt = now;
        m_netDebugger.BytesIn = stats.BytesReceived;
        m_netDebugger.BytesOut = stats.BytesSent;
    }
    else if( now - m_netDebugger.RateAt >= 1.0 )
    {
        const double seconds = now - m_netDebugger.RateAt;
        m_netDebugger.KbpsIn = static_cast<double>( stats.BytesReceived - m_netDebugger.BytesIn ) * 8.0 / 1000.0 / seconds;
        m_netDebugger.KbpsOut = static_cast<double>( stats.BytesSent - m_netDebugger.BytesOut ) * 8.0 / 1000.0 / seconds;
        m_netDebugger.RateAt = now;
        m_netDebugger.BytesIn = stats.BytesReceived;
        m_netDebugger.BytesOut = stats.BytesSent;
    }

    ImGui::SetNextWindowSize( ImVec2( 640.f, 420.f ), ImGuiCond_FirstUseEver );
    if( !ImGui::Begin( "Net Debugger", &m_netDebugger.Open ) )
    {
        ImGui::End();
        return;
    }
    ImGui::Text( "Client #%u, tick %u (%s)", client->GetClientId(), client->GetClock().GetTick(), client->GetState() == NetClient::State::Connected ? "connected" : "not connected" );
    ImGui::Text( "RTT %.1f ms (+-%.1f), loss %.1f%%, in %.1f kbps, out %.1f kbps", stats.RoundTripMs, stats.RoundTripDeviationMs, stats.PacketLoss * 100.f, m_netDebugger.KbpsIn,
        m_netDebugger.KbpsOut );
    ImGui::Text( "Slack %.2f (target %.2f), dilation %+.2f%%, %d resyncs, %llu hitches", client->GetServerSlack(), client->GetClock().GetTargetSlack(),
        client->GetClock().GetDilation() * 100.f, client->GetClock().GetResyncCount(), static_cast<unsigned long long>( client->GetHitchCount() ) );
    // The blob channel (ME-323): the counts, and a bar for each blob on its way.
    ImGui::Text( "Blobs: %llu received, %llu sent (%llu acked), %llu cancelled (%llu by the server); slices %llu sent, %llu resent, %llu duplicate",
        static_cast<unsigned long long>( stats.BlobsReceived ), static_cast<unsigned long long>( stats.BlobsSent ), static_cast<unsigned long long>( stats.BlobsAcked ),
        static_cast<unsigned long long>( stats.BlobsCancelled ), static_cast<unsigned long long>( stats.BlobsCancelledByRemote ),
        static_cast<unsigned long long>( stats.BlobSlicesSent ), static_cast<unsigned long long>( stats.BlobSlicesResent ),
        static_cast<unsigned long long>( stats.BlobSlicesDuplicate ) );
    for( const NetBlobProgress& blob : client->GetConnection().GetBlobProgress() )
    {
        char label[128];
        std::snprintf( label, sizeof( label ), "%s #%u: %.1f / %.1f KB%s", blob.Outgoing ? "out" : "in", blob.Id, static_cast<double>( blob.DoneBytes ) / 1024.0,
            static_cast<double>( blob.Size ) / 1024.0, blob.Outgoing && !blob.Started ? " (queued)" : "" );
        ImGui::ProgressBar( blob.Size > 0 ? static_cast<float>( static_cast<double>( blob.DoneBytes ) / static_cast<double>( blob.Size ) ) : 0.f, ImVec2( -1.f, 0.f ), label );
    }
    const NetPredictor* predictor = InNet.GetPredictor();
    if( !predictor )
    {
        ImGui::TextUnformatted( "Nothing predicted (no pawn owned)." );
        ImGui::End();
        return;
    }
    const Vector3 offset = predictor->GetVisualOffset();
    ImGui::Text( "Prediction: %d corrections, %d matches, %d initial sync; last resim %d ticks in %.3f ms; visual offset %.1f mm", predictor->GetCorrectionCount(),
        predictor->GetMatchCount(), predictor->GetInitialSyncCount(), predictor->GetLastResimTicks(), predictor->GetLastResimMilliseconds(), offset.Length() * 1000.f );

    // The timeline: the last 10 s of reconciled ticks, one column each (green match, red correction,
    // grey initial sync, dark not reconciled). Hover for the tick; click a mark to inspect it.
    uint32_t newest = 0;
    if( predictor->GetNewestReconciled( newest ) )
    {
        const uint32_t span = predictor->GetTimelineTicks();
        const uint32_t first = newest + 1 >= span ? newest + 1 - span : 0;
        const float width = std::max( 100.f, ImGui::GetContentRegionAvail().x );
        const float height = 28.f;
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton( "##MispredictTimeline", ImVec2( width, height ) );
        const bool hovered = ImGui::IsItemHovered();
        const bool clicked = ImGui::IsItemClicked();
        ImDrawList* draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled( origin, ImVec2( origin.x + width, origin.y + height ), IM_COL32( 25, 25, 30, 255 ) );
        const float perTick = width / static_cast<float>( span );
        auto xOf = [&]( uint32_t InTick ) { return origin.x + static_cast<float>( InTick - first ) * perTick; };
        // Every reconciled tick from the timeline itself: under a steady desync only the newest
        // kMaxMispredicts mismatches keep a record, and the rest must still read red.
        for( uint32_t tick = first; tick <= newest; ++tick )
        {
            const NetPredictOutcome outcome = predictor->GetOutcome( tick );
            if( outcome == NetPredictOutcome::Match )
            {
                draw->AddRectFilled( ImVec2( xOf( tick ), origin.y + height * 0.55f ), ImVec2( xOf( tick ) + std::max( perTick, 1.f ), origin.y + height ), IM_COL32( 60, 170, 80, 255 ) );
            }
            else if( outcome == NetPredictOutcome::Correction || outcome == NetPredictOutcome::InitialSync )
            {
                const ImU32 color = outcome == NetPredictOutcome::InitialSync ? IM_COL32( 150, 150, 150, 255 ) : IM_COL32( 230, 60, 50, 255 );
                draw->AddRectFilled( ImVec2( xOf( tick ), origin.y ), ImVec2( xOf( tick ) + std::max( perTick, 1.f ), origin.y + height ), color );
            }
        }
        // The mismatches with a record on top, wide enough to see and click.
        for( const NetMispredictRecord& record : predictor->GetMispredicts() )
        {
            if( record.Tick < first )
            {
                continue;
            }
            const bool selected = m_netDebugger.HasSelected && m_netDebugger.Selected == record.Tick;
            const ImU32 color = record.InitialSync ? IM_COL32( 150, 150, 150, 255 ) : selected ? IM_COL32( 255, 220, 60, 255 ) : IM_COL32( 230, 60, 50, 255 );
            draw->AddRectFilled( ImVec2( xOf( record.Tick ) - 1.f, origin.y ), ImVec2( xOf( record.Tick ) + std::max( perTick, 2.f ), origin.y + height ), color );
        }
        if( hovered )
        {
            const float mouse = ImGui::GetIO().MousePos.x;
            const uint32_t tick = first + static_cast<uint32_t>( std::clamp( ( mouse - origin.x ) / perTick, 0.f, static_cast<float>( span - 1 ) ) );
            // The nearest mismatch within 4 px.
            const NetMispredictRecord* closest = nullptr;
            for( const NetMispredictRecord& record : predictor->GetMispredicts() )
            {
                if( record.Tick >= first && std::abs( xOf( record.Tick ) - mouse ) <= 4.f && ( !closest || std::abs( xOf( record.Tick ) - mouse ) < std::abs( xOf( closest->Tick ) - mouse ) ) )
                {
                    closest = &record;
                }
            }
            static const char* names[] = { "not reconciled", "match", "correction", "initial sync" };
            ImGui::BeginTooltip();
            if( closest )
            {
                ImGui::Text( "tick %u: %s", closest->Tick, closest->InitialSync ? "initial sync" : "correction" );
                ImGui::TextUnformatted( closest->Describe().c_str() );
            }
            else
            {
                const NetPredictOutcome outcome = predictor->GetOutcome( tick );
                ImGui::Text( "tick %u: %s", tick, names[static_cast<int>( outcome )] );
                if( ( outcome == NetPredictOutcome::Correction || outcome == NetPredictOutcome::InitialSync ) && !predictor->FindMispredict( tick ) )
                {
                    ImGui::Text( "(no record: only the newest %zu mismatches keep one)", NetPredictor::kMaxMispredicts );
                }
            }
            ImGui::EndTooltip();
            if( clicked && closest )
            {
                m_netDebugger.HasSelected = true;
                m_netDebugger.Selected = closest->Tick;
            }
        }
        ImGui::Text( "ticks %u..%u (%.0f s)", first, newest, NetPredictor::kTimelineSeconds );
    }
    else
    {
        ImGui::TextUnformatted( "No tick reconciled yet." );
    }

    // The mark picked (else the newest mismatch): what differed, the commands, export.
    const NetMispredictRecord* record = m_netDebugger.HasSelected ? predictor->FindMispredict( m_netDebugger.Selected ) : nullptr;
    if( !record && !predictor->GetMispredicts().empty() )
    {
        record = &predictor->GetMispredicts().back();
    }
    ImGui::Separator();
    if( !record )
    {
        ImGui::TextUnformatted( "No mispredictions in the last 10 s." );
        ImGui::End();
        return;
    }
    ImGui::Text( "Tick %u%s: %s", record->Tick, record->InitialSync ? " (initial sync)" : "", record->Describe().c_str() );
    if( ImGui::BeginTable( "##MispredictFields", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_SizingStretchProp ) )
    {
        ImGui::TableSetupColumn( "" );
        ImGui::TableSetupColumn( "predicted" );
        ImGui::TableSetupColumn( "server" );
        ImGui::TableSetupColumn( "delta" );
        ImGui::TableHeadersRow();
        auto row = [&]( const char* InName, const Vector3& InPredicted, const Vector3& InServer, float InScale, const char* InUnit ) {
            const Vector3 delta = InServer - InPredicted;
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted( InName );
            ImGui::TableNextColumn();
            ImGui::Text( "%.4f, %.4f, %.4f", InPredicted.x, InPredicted.y, InPredicted.z );
            ImGui::TableNextColumn();
            ImGui::Text( "%.4f, %.4f, %.4f", InServer.x, InServer.y, InServer.z );
            ImGui::TableNextColumn();
            ImGui::Text( "%.1f %s", delta.Length() * InScale, InUnit );
        };
        row( "position", record->Predicted.Position, record->Authoritative.Position, 1000.f, "mm" );
        row( "velocity", record->Predicted.Velocity, record->Authoritative.Velocity, 1.f, "m/s" );
        row( "ground normal", record->Predicted.GroundNormal, record->Authoritative.GroundNormal, 1.f, "" );
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted( "grounded" );
        ImGui::TableNextColumn();
        ImGui::TextUnformatted( record->Predicted.Grounded ? "yes" : "no" );
        ImGui::TableNextColumn();
        ImGui::TextUnformatted( record->Authoritative.Grounded ? "yes" : "no" );
        ImGui::TableNextColumn();
        ImGui::TextUnformatted( record->Predicted.Grounded == record->Authoritative.Grounded ? "" : "differs" );
        ImGui::EndTable();
    }
    for( const auto& [begin, end] : record->ScriptDiffs )
    {
        ImGui::Text( "[Predicted] script bytes %u-%u differ", begin, end - 1 );
    }
    ImGui::Text( "%zu ticks resimulated in %.3f ms; the present moved %.1f mm (the visual offset hides it)", record->Resimulated.size(), record->ResimMilliseconds,
        record->PresentJump.Length() * 1000.f );
    if( ImGui::Button( "Export" ) )
    {
        bool exported = false;
        m_netDebugger.LastExport = ExportMispredict( InNet, record->Tick, exported );
    }
    if( !m_netDebugger.LastExport.empty() )
    {
        ImGui::SameLine();
        ImGui::TextUnformatted( m_netDebugger.LastExport.c_str() );
    }
    if( ImGui::TreeNode( "Commands", "Commands (%zu, from tick %u)", record->Commands.size(), record->Tick ) )
    {
        for( const InputCommand& command : record->Commands )
        {
            ImGui::Text( "%u: buttons %08x, axes %.3f %.3f %.3f %.3f%s", command.Tick, command.Buttons, command.Axes[0], command.Axes[1], command.Axes[2], command.Axes[3],
                command.HasAim ? "" : " (no aim)" );
        }
        ImGui::TreePop();
    }
    ImGui::End();
#else
    (void)InNet;
#endif
}


void NetLaunch::DrawReplayControls( NetCore& InNet )
{
    if( m_viewer && InNet.GetPlayback() )
    {
        m_viewer->DrawUI( InNet );
    }
}


void NetLaunch::UpdateReplayCamera( NetCore& InNet, float InDeltaSeconds )
{
    if( m_viewer && InNet.GetPlayback() )
    {
        m_viewer->UpdateCamera( InNet, InDeltaSeconds );
    }
}


void NetLaunch::Shutdown( NetCore& InNet, double InNow )
{
    if( !m_requested )
    {
        return;
    }
    // --replay-check promises an exit code: a check that never ran (the playhead never rested on its
    // tick before the run ended) is a failure, not a pass.
    if( m_replayCheck && !m_replayChecked && m_error.empty() )
    {
        m_error = "--replay-check never ran: the replay never rested on its tick";
        Say( CLog::LogType::Error, "Replay: " + m_error );
    }
    // So does --resim-check: cut short (a signal, --frames, a timeout) it has no verdict, not 0.
    if( CommandLine::Has( "--resim-check" ) && IsReplaying() && m_error.empty() && ( !m_viewer || m_viewer->GetExitCode() < 0 ) )
    {
        m_error = "--resim-check never finished: the run ended before every character was checked";
        Say( CLog::LogType::Error, "Replay: " + m_error );
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
    if( m_viewer )
    {
        m_viewerExitCode = m_viewer->GetExitCode();
        m_viewer.reset();
    }
    InNet.Shutdown();
    m_conditioned.reset();
    m_socket.reset();
}
