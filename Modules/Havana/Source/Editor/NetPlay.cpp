#include "NetPlay.h"

#if USING( ME_EDITOR )

#include "CLog.h"
#include "Components/Networking/SingletonInput.h"
#include "Core/CommandLine.h"
#include "Cores/NetCore.h"
#include "Engine/AutomationRunner.h"
#include "Engine/DevConsole.h"
#include "Engine/Engine.h"
#include "Engine/NetLaunch.h"
#include "Engine/Project.h"
#include "Engine/World.h"
#include "JSON.h"
#include "Net/UdpTransport.h"
#include "Utils/EditorConfig.h"
#include "World/Scene.h"
#include "Editor/UndoStack.h"
#if USING( ME_SCRIPTING )
#include "Scripting/ScriptNet.h"
#endif
#include <ImGuiTheme.h>
#include <imgui.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optick.h>

#if !USING( ME_PLATFORM_WINDOWS )
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#if USING( ME_PLATFORM_LINUX )
#include <sys/prctl.h>
#endif
#endif

namespace
{
    constexpr double kQuitGraceSeconds = 3.0;   // stdin closed: the child quits by itself
    constexpr double kTermGraceSeconds = 2.0;   // then SIGTERM; after this SIGKILL

    double Now()
    {
        return std::chrono::duration<double>( std::chrono::steady_clock::now().time_since_epoch() ).count();
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

    std::string Describe( const NetConditions& InConditions )
    {
        char text[160];
        std::snprintf( text, sizeof( text ), "%.0f+-%.0f ms each way, %.1f%% loss, %.1f%% duplicates", InConditions.LatencyMs, InConditions.JitterMs,
            InConditions.Loss * 100.f, InConditions.Duplicate * 100.f );
        return text;
    }

    // A child's stdout line: CLog writes "[Info]: message" ("[Warning]: ", "[! Error !]: ", ...).
    // The message, and the type it was logged with (Info for lines without one: a multi-line
    // message's continuation, the runtime's own output).
    std::string SplitLogType( const std::string& InLine, CLog::LogType& OutType )
    {
        static const std::pair<const char*, CLog::LogType> kTypes[] = { { "[Info]: ", CLog::LogType::Info }, { "[Warning]: ", CLog::LogType::Warning },
            { "[! Error !]: ", CLog::LogType::Error }, { "[Debug]: ", CLog::LogType::Debug }, { "[Trace]: ", CLog::LogType::Trace } };
        for( const auto& [prefix, type] : kTypes )
        {
            const size_t length = std::strlen( prefix );
            if( InLine.compare( 0, length, prefix ) == 0 )
            {
                OutType = type;
                return InLine.substr( length );
            }
        }
        OutType = InLine.find( "--- ASSERT ---" ) != std::string::npos ? CLog::LogType::Error : CLog::LogType::Info;
        return InLine;
    }
}


struct NetPlay::Child
{
    ChildInfo Info;
    int StdinFd = -1;
    int StdoutFd = -1;
    std::string Pending;
    std::ofstream Log;
    std::map<uint64_t, std::string> Replies;
    double StopAt = -1.0;   // when Stop closed its stdin (< 0: not stopping)
    bool Termed = false;
    bool Killed = false;
};


NetPlay& NetPlay::Get()
{
    static NetPlay instance;
    return instance;
}


NetPlay::NetPlay() = default;


NetPlay::~NetPlay()
{
    Shutdown();
}


void NetPlay::LoadSettings()
{
    m_settings = Settings();
    // The editor's own --net-* are where the link starts.
    m_settings.Conditions.LatencyMs = std::max( 0.f, CommandLine::GetFloat( "--net-lag", 0.f ) );
    m_settings.Conditions.JitterMs = std::max( 0.f, CommandLine::GetFloat( "--net-jitter", 0.f ) );
    m_settings.Conditions.Loss = ParseFraction( CommandLine::GetString( "--net-loss", "0" ) );
    m_settings.Conditions.Seed = static_cast<uint64_t>( std::max( 1, CommandLine::GetInt( "--net-seed", 1 ) ) );
    // Unattended runs (edscripts) start offline whatever the user last chose: a flow asks with play-net.
    if( AutomationRunner::IsUnattendedRun() )
    {
        return;
    }
    const EditorConfig& config = EditorConfig::GetInstance();
    m_settings.Clients = std::clamp( config.GetPreference<int>( "Play.NetClients", 0 ), 0, kMaxClients );
    m_settings.ClientArgs = config.GetPreference<std::string>( "Play.NetClientArgs", "" );
    m_settings.ShowClientLogs = config.GetPreference<bool>( "Play.NetClientLogs", true );
    if( !CommandLine::Has( "--net-lag" ) && !CommandLine::Has( "--net-jitter" ) && !CommandLine::Has( "--net-loss" ) )
    {
        m_settings.Conditions.LatencyMs = config.GetPreference<float>( "Play.NetLagMs", 0.f );
        m_settings.Conditions.JitterMs = config.GetPreference<float>( "Play.NetJitterMs", 0.f );
        m_settings.Conditions.Loss = config.GetPreference<float>( "Play.NetLoss", 0.f );
        m_settings.Conditions.Duplicate = config.GetPreference<float>( "Play.NetDuplicate", 0.f );
    }
}


void NetPlay::SaveSettings() const
{
    if( AutomationRunner::IsUnattendedRun() )
    {
        return;
    }
    EditorConfig& config = EditorConfig::GetInstance();
    config.SetPreference( "Play.NetClients", m_settings.Clients );
    config.SetPreference( "Play.NetClientArgs", m_settings.ClientArgs );
    config.SetPreference( "Play.NetClientLogs", m_settings.ShowClientLogs );
    config.SetPreference( "Play.NetLagMs", m_settings.Conditions.LatencyMs );
    config.SetPreference( "Play.NetJitterMs", m_settings.Conditions.JitterMs );
    config.SetPreference( "Play.NetLoss", m_settings.Conditions.Loss );
    config.SetPreference( "Play.NetDuplicate", m_settings.Conditions.Duplicate );
}


void NetPlay::SetPendingPlay( int InClients, const std::string& InExtraArgs )
{
    // Below 0 clears it (a play-net that didn't start): the settings decide the next Play again.
    m_pendingClients = InClients < 0 ? -1 : std::min( InClients, kMaxClients );
    m_pendingArgs = InExtraArgs;
}


bool NetPlay::IsWanted() const
{
    return ( m_pendingClients >= 0 ? m_pendingClients : m_settings.Clients ) > 0;
}


std::string NetPlay::FindPlayer() const
{
    const std::string chosen = CommandLine::GetString( "--pie-player" );
    if( !chosen.empty() )
    {
        return chosen;
    }
#if USING( ME_PLATFORM_LINUX )
    // .build/Editor_<Config>/Havana -> .build/Game_linux_<Config>/Player
    std::error_code error;
    const std::filesystem::path editor = std::filesystem::read_symlink( "/proc/self/exe", error );
    if( error )
    {
        return {};
    }
    const std::string folder = editor.parent_path().filename().string();
    const std::string config = folder.rfind( "Editor_", 0 ) == 0 ? folder.substr( 7 ) : std::string( "Debug" );
    return ( editor.parent_path().parent_path() / ( "Game_linux_" + config ) / "Player" ).string();
#else
    return {};
#endif
}


bool NetPlay::Start( NetCore& InNet, uint16_t InTickRate, std::string& OutError )
{
    OPTICK_EVENT( "NetPlay::Start" );
    const int clients = m_pendingClients >= 0 ? m_pendingClients : m_settings.Clients;
    std::string extraArgs = m_settings.ClientArgs;
    if( m_pendingClients >= 0 && !m_pendingArgs.empty() )
    {
        extraArgs += ( extraArgs.empty() ? "" : " " ) + m_pendingArgs;
    }
    m_pendingClients = -1;
    m_pendingArgs.clear();
    m_lastError.clear();
    auto fail = [&]( const std::string& InWhy ) {
        m_lastError = InWhy;
        OutError = InWhy;
        YIKES( "PIE: " + InWhy );
        return false;
    };
    if( clients <= 0 )
    {
        return fail( "no clients asked for" );
    }
#if USING( ME_PLATFORM_WINDOWS )
    return fail( "Play with clients is Linux / POSIX only for now" );
#else
    if( m_hosting || InNet.IsActive() )
    {
        return fail( "a network session is already running" );
    }
    for( const std::unique_ptr<Child>& child : m_children )
    {
        if( child->Info.Running )
        {
            return fail( "the last Play's clients haven't quit yet (pid " + std::to_string( child->Info.Pid ) + ")" );
        }
    }
    Scene* scene = GetEngine().CurrentScene;
    if( !scene || scene->IsNewScene() )
    {
        return fail( "save the scene first: the clients load it from disk" );
    }
    const std::string scenePath = scene->FilePath.GetLocalPathString();
    if( UndoStack::Get().IsDirty() )
    {
        BRUH( "PIE: the scene has unsaved changes; the clients load the saved " + scenePath + " (networked entities that differ are turned away at the handshake)" );
    }
    const std::string player = FindPlayer();
    std::error_code fsError;
    if( player.empty() || !std::filesystem::is_regular_file( player, fsError ) )
    {
        return fail( "no game Player to start the clients with (" + ( player.empty() ? std::string( "--pie-player <path>" ) : player )
            + "): build it (make Player config=game_linux_debug) or pass --pie-player" );
    }

    // The listen server: loopback only, any free port, conditioned both ways at this end.
    m_socket = std::make_unique<UdpTransport>( 0, "127.0.0.1" );
    if( !m_socket->IsOpen() )
    {
        const std::string why = m_socket->GetError();
        m_socket.reset();
        return fail( "the UDP socket didn't open: " + why );
    }
    m_port = m_socket->GetPort();
    // Seeded once a session (--net-seed, each direction its own sequence); live changes carry on.
    m_conditioned = std::make_unique<ConditionedTransport>( *m_socket, m_settings.Conditions );
    ApplyConditions( true );
    // What a Player --server gets from the command line too: --net-timeout, --net-debug-clients.
    NetLaunch::ApplySessionOptions( InNet, true );
    InNet.SetCommandSchema( InNet.GetWorld().Singleton<SingletonInput>().GetCommandSchema() );
#if USING( ME_SCRIPTING )
    InNet.SetScriptGlue( MakeScriptNetGlue() );
#endif
    if( !InNet.Host( *m_conditioned, InTickRate ) )
    {
        m_conditioned.reset();
        m_socket.reset();
        return fail( "the session didn't start" );
    }
    // The replay ring killcams are cut from (ME-323), or --record-replay's file (ME-316), as a Player --server keeps.
    NetLaunch::StartRecording( InNet );
    // NET-7: the clients run on wall-clock time; a scaled server would drift past what their clocks dilate.
    if( GetEngine().GetTimeScale() != 1.f )
    {
        BRUH( "PIE: time scale back to x1 while the clients play" );
        GetEngine().SetTimeScale( 1.f );
    }
    m_net = &InNet;
    m_hosting = true;
    m_lastAccepted = 0;
    m_lastLeft = 0;
    CLog::Log( CLog::LogType::Info, "PIE: hosting " + scenePath + " on 127.0.0.1:" + std::to_string( m_port ) + " at " + std::to_string( InTickRate ) + " Hz ("
        + Describe( m_settings.Conditions ) + "), starting " + std::to_string( clients ) + " client(s): " + player );

    const std::string root = Project::GetRoot();
    std::filesystem::create_directories( root + "/.tmp/PIE", fsError );
    std::vector<std::string> extra = DevConsole::Split( extraArgs );
    m_children.clear();
    for( int index = 1; index <= clients; ++index )
    {
        std::vector<std::string> args = { player, "--project", root, "--scene", scenePath, "--connect", "127.0.0.1:" + std::to_string( m_port ), "--control-stdin",
            "--pie-editor", std::to_string( getpid() ), "--window-mode", "windowed", "--width", "640", "--height", "360",
            "--window-x", std::to_string( 40 + ( index - 1 ) % 3 * 660 ), "--window-y", std::to_string( 60 + ( index - 1 ) / 3 * 400 ) };
        if( AutomationRunner::IsUnattendedRun() )
        {
            // An unattended editor's (an edscript's) clients are too: silent, the player's saved
            // settings neither read nor written.
            args.push_back( "--transient" );
        }
        else
        {
            // Each client's user state (settings, window, crash reports) on its own: never the
            // player's, and never the same file as another client's.
            args.push_back( "--user-dir" );
            args.push_back( root + "/.tmp/PIE/User" + std::to_string( index ) );
        }
        args.insert( args.end(), extra.begin(), extra.end() );
        std::string error;
        if( !SpawnChild( index, args, error ) )
        {
            Stop( InNet );
            return fail( "client " + std::to_string( index ) + " didn't start: " + error );
        }
    }
    return true;
#endif
}


bool NetPlay::SpawnChild( int InIndex, const std::vector<std::string>& InArgs, std::string& OutError )
{
#if USING( ME_PLATFORM_WINDOWS )
    OutError = "not supported here";
    return false;
#else
    int toChild[2] = { -1, -1 };
    int fromChild[2] = { -1, -1 };
    // Close-on-exec from the start: a process another thread forks meanwhile (a script build) must
    // not inherit them, or the child's stdin would stay open after Stop closes it.
#if USING( ME_PLATFORM_LINUX )
    const bool piped = pipe2( toChild, O_CLOEXEC ) == 0 && pipe2( fromChild, O_CLOEXEC ) == 0;
#else
    const bool piped = pipe( toChild ) == 0 && pipe( fromChild ) == 0;
    for( int fd : { toChild[0], toChild[1], fromChild[0], fromChild[1] } )
    {
        if( fd >= 0 ) fcntl( fd, F_SETFD, FD_CLOEXEC );
    }
#endif
    if( !piped )
    {
        OutError = std::string( "pipe: " ) + std::strerror( errno );
        for( int fd : { toChild[0], toChild[1], fromChild[0], fromChild[1] } )
        {
            if( fd >= 0 ) close( fd );
        }
        return false;
    }
    // Everything the child needs is made before fork: after it, only async-signal-safe calls.
    std::vector<char*> argv;
    for( const std::string& arg : InArgs )
    {
        argv.push_back( const_cast<char*>( arg.c_str() ) );
    }
    argv.push_back( nullptr );
    const pid_t parent = getpid();
    const long maxFd = std::max( 256L, sysconf( _SC_OPEN_MAX ) );
    const pid_t pid = fork();
    if( pid < 0 )
    {
        OutError = std::string( "fork: " ) + std::strerror( errno );
        for( int fd : { toChild[0], toChild[1], fromChild[0], fromChild[1] } ) close( fd );
        return false;
    }
    if( pid == 0 )
    {
#if USING( ME_PLATFORM_LINUX )
        // Dies with the editor, however it ends (and if it already ended, don't start).
        prctl( PR_SET_PDEATHSIG, SIGKILL );
#endif
        if( getppid() != parent )
        {
            _exit( 1 );
        }
        dup2( toChild[0], STDIN_FILENO );
        dup2( fromChild[1], STDOUT_FILENO );
        dup2( fromChild[1], STDERR_FILENO );
        // Nothing of the editor's (its sockets, files, the other children's pipes) goes with it.
#if defined( SYS_close_range )
        if( syscall( SYS_close_range, 3u, ~0u, 0u ) != 0 )
#endif
        {
            for( long fd = 3; fd < maxFd; ++fd )
            {
                close( static_cast<int>( fd ) );
            }
        }
        execv( argv[0], argv.data() );
        _exit( 127 );
    }
    close( toChild[0] );
    close( fromChild[1] );
    fcntl( fromChild[0], F_SETFL, fcntl( fromChild[0], F_GETFL, 0 ) | O_NONBLOCK );
    // A child gone before reading what we wrote must not kill the editor with SIGPIPE.
    signal( SIGPIPE, SIG_IGN );

    auto child = std::make_unique<Child>();
    child->Info.Index = InIndex;
    child->Info.Pid = static_cast<int>( pid );
    child->Info.Running = true;
    child->Info.LogPath = Project::GetRoot() + "/.tmp/PIE/Client" + std::to_string( InIndex ) + ".log";
    child->StdinFd = toChild[1];
    child->StdoutFd = fromChild[0];
    child->Log.open( child->Info.LogPath, std::ios::trunc );
    std::string line;
    for( const std::string& arg : InArgs )
    {
        line += ( line.empty() ? "" : " " ) + ( arg.find_first_of( " \t\"'" ) == std::string::npos && !arg.empty() ? arg : "'" + arg + "'" );
    }
    child->Log << "# " << line << "\n";
    CLog::Log( CLog::LogType::Info, "PIE: client " + std::to_string( InIndex ) + " started (pid " + std::to_string( pid ) + "), its log: " + child->Info.LogPath );
    m_children.push_back( std::move( child ) );
    return true;
#endif
}


void NetPlay::HandleLine( Child& InChild, const std::string& InLine )
{
    // An answer: "[control] <id> <JSON string>".
    if( InLine.rfind( "[control] ", 0 ) == 0 )
    {
        const char* text = InLine.c_str() + 10;
        char* end = nullptr;
        const uint64_t id = std::strtoull( text, &end, 10 );
        const json value = json::parse( end ? end : "", nullptr, false );
        InChild.Replies[id] = value.is_string() ? value.get<std::string>() : std::string( end ? end : "" );
        InChild.Log << InLine << "\n";
        return;
    }
    InChild.Log << InLine << "\n";
    CLog::LogType type = CLog::LogType::Info;
    const std::string message = SplitLogType( InLine, type );
    const std::string connected = "Net: connected as client #";
    if( const size_t at = message.find( connected ); at != std::string::npos )
    {
        InChild.Info.ClientId = std::atoi( message.c_str() + at + connected.size() );
    }
    if( m_settings.ShowClientLogs && !message.empty() )
    {
        CLog::Log( type, "[client " + std::to_string( InChild.Info.Index ) + "] " + message );
    }
}


void NetPlay::PumpChild( Child& InChild )
{
#if !USING( ME_PLATFORM_WINDOWS )
    if( InChild.StdoutFd < 0 )
    {
        return;
    }
    char buffer[8192];
    for( ;; )
    {
        const ssize_t got = read( InChild.StdoutFd, buffer, sizeof( buffer ) );
        if( got > 0 )
        {
            InChild.Pending.append( buffer, static_cast<size_t>( got ) );
            continue;
        }
        if( got == 0 || ( errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR ) )
        {
            close( InChild.StdoutFd );
            InChild.StdoutFd = -1;
        }
        break;
    }
    size_t newline = 0;
    while( ( newline = InChild.Pending.find( '\n' ) ) != std::string::npos )
    {
        std::string line = InChild.Pending.substr( 0, newline );
        InChild.Pending.erase( 0, newline + 1 );
        if( !line.empty() && line.back() == '\r' )
        {
            line.pop_back();
        }
        HandleLine( InChild, line );
    }
    if( InChild.StdoutFd < 0 && !InChild.Pending.empty() )
    {
        HandleLine( InChild, InChild.Pending );
        InChild.Pending.clear();
    }
    InChild.Log.flush();
#endif
}


void NetPlay::ReapChildren( double InNow )
{
#if !USING( ME_PLATFORM_WINDOWS )
    for( std::unique_ptr<Child>& child : m_children )
    {
        if( !child->Info.Running )
        {
            continue;
        }
        int status = 0;
        const pid_t done = waitpid( child->Info.Pid, &status, WNOHANG );
        if( done == child->Info.Pid || ( done < 0 && errno == ECHILD ) )
        {
            PumpChild( *child );
            child->Info.Running = false;
            child->Info.ExitCode = done < 0 ? -1 : WIFEXITED( status ) ? WEXITSTATUS( status ) : 128 + ( WIFSIGNALED( status ) ? WTERMSIG( status ) : 0 );
            if( child->StdinFd >= 0 )
            {
                close( child->StdinFd );
                child->StdinFd = -1;
            }
            if( child->StdoutFd >= 0 )
            {
                close( child->StdoutFd );
                child->StdoutFd = -1;
            }
            const bool expected = child->StopAt >= 0.0;
            const std::string how = WIFSIGNALED( status ) ? "killed by signal " + std::to_string( WTERMSIG( status ) ) : "exit code " + std::to_string( child->Info.ExitCode );
            CLog::Log( expected && !child->Killed && !child->Termed ? CLog::LogType::Info : CLog::LogType::Warning,
                "PIE: client " + std::to_string( child->Info.Index ) + ( expected ? " quit" : " exited on its own" ) + " with " + how + " (pid " + std::to_string( child->Info.Pid ) + ")" );
            child->Log.close();
            continue;
        }
        if( child->StopAt < 0.0 )
        {
            continue;
        }
        // Stopping: it had kQuitGraceSeconds to quit once its stdin closed, then SIGTERM, then SIGKILL.
        if( !child->Termed && InNow - child->StopAt > kQuitGraceSeconds )
        {
            child->Termed = true;
            CLog::Log( CLog::LogType::Warning, "PIE: client " + std::to_string( child->Info.Index ) + " didn't quit; SIGTERM" );
            kill( child->Info.Pid, SIGTERM );
        }
        else if( child->Termed && !child->Killed && InNow - child->StopAt > kQuitGraceSeconds + kTermGraceSeconds )
        {
            child->Killed = true;
            CLog::Log( CLog::LogType::Warning, "PIE: client " + std::to_string( child->Info.Index ) + " still running; SIGKILL" );
            kill( child->Info.Pid, SIGKILL );
        }
    }
#endif
}


void NetPlay::Update()
{
    OPTICK_EVENT( "NetPlay::Update" );
    for( std::unique_ptr<Child>& child : m_children )
    {
        PumpChild( *child );
    }
    ReapChildren( Now() );
    if( m_hosting && m_net )
    {
        if( NetServer* server = m_net->GetServer() )
        {
            if( server->GetAcceptedCount() != m_lastAccepted )
            {
                m_lastAccepted = server->GetAcceptedCount();
                CLog::Log( CLog::LogType::Info, "PIE: client joined (" + std::to_string( server->GetClientCount() ) + " connected)" );
            }
            const uint64_t left = server->GetDisconnectCount() + server->GetTimeoutCount();
            if( left != m_lastLeft )
            {
                m_lastLeft = left;
                CLog::Log( CLog::LogType::Info, "PIE: client left (" + std::to_string( server->GetClientCount() ) + " connected)" );
            }
        }
    }
}


void NetPlay::Stop( NetCore& InNet )
{
    if( m_hosting )
    {
        // Goodbyes first, so every child hears the Disconnect rather than waiting out a timeout.
        InNet.Close( Now() );
        InNet.Shutdown();
        m_hosting = false;
        m_net = nullptr;
        CLog::Log( CLog::LogType::Info, "PIE: the listen server closed" );
    }
    m_conditioned.reset();
    m_socket.reset();
#if !USING( ME_PLATFORM_WINDOWS )
    const double now = Now();
    for( std::unique_ptr<Child>& child : m_children )
    {
        if( child->Info.Running && child->StopAt < 0.0 )
        {
            child->StopAt = now;
            // A closed stdin is the child's cue to quit (ControlChannel).
            if( child->StdinFd >= 0 )
            {
                close( child->StdinFd );
                child->StdinFd = -1;
            }
        }
    }
#endif
    ReapChildren( Now() );
}


void NetPlay::Shutdown()
{
#if !USING( ME_PLATFORM_WINDOWS )
    if( m_hosting && m_net )
    {
        Stop( *m_net );
    }
    m_conditioned.reset();
    m_socket.reset();
    bool anyRunning = false;
    for( std::unique_ptr<Child>& child : m_children )
    {
        if( child->Info.Running && child->StopAt < 0.0 )
        {
            child->StopAt = Now();
            if( child->StdinFd >= 0 )
            {
                close( child->StdinFd );
                child->StdinFd = -1;
            }
        }
        anyRunning = anyRunning || child->Info.Running;
    }
    // Bounded: the escalation runs to SIGKILL, which can't be ignored.
    const double deadline = Now() + kQuitGraceSeconds + kTermGraceSeconds + 2.0;
    while( anyRunning && Now() < deadline )
    {
        for( std::unique_ptr<Child>& child : m_children )
        {
            PumpChild( *child );
        }
        ReapChildren( Now() );
        anyRunning = std::any_of( m_children.begin(), m_children.end(), []( const std::unique_ptr<Child>& InChild ) { return InChild->Info.Running; } );
        if( anyRunning )
        {
            usleep( 20000 );
        }
    }
#endif
}


size_t NetPlay::GetConnectedCount() const
{
    if( !m_hosting || !m_net )
    {
        return 0;
    }
    const NetServer* server = m_net->GetServer();
    return server ? server->GetClientCount() : 0;
}


std::vector<NetPlay::ChildInfo> NetPlay::GetChildren() const
{
    std::vector<ChildInfo> out;
    for( const std::unique_ptr<Child>& child : m_children )
    {
        out.push_back( child->Info );
    }
    return out;
}


int NetPlay::GetClientId( int InIndex ) const
{
    for( const std::unique_ptr<Child>& child : m_children )
    {
        if( child->Info.Index == InIndex )
        {
            return child->Info.ClientId;
        }
    }
    return 0;
}


float NetPlay::GetRoundTripMs( int InIndex ) const
{
    const int id = GetClientId( InIndex );
    if( !m_hosting || !m_net || id <= 0 )
    {
        return -1.f;
    }
    NetServer* server = m_net->GetServer();
    const NetServer::Client* client = server ? server->FindClient( static_cast<uint8_t>( id ) ) : nullptr;
    return client && client->Connection ? client->Connection->GetStats().RoundTripMs : -1.f;
}


void NetPlay::ApplyConditions( bool InReseed )
{
    if( !m_conditioned )
    {
        return;
    }
    NetConditions inbound = m_settings.Conditions;
    inbound.Seed = m_settings.Conditions.Seed + 1;
    m_conditioned->SetConditions( m_settings.Conditions, InReseed );
    m_conditioned->SetInboundConditions( inbound, InReseed );
}


void NetPlay::SetConditions( const NetConditions& InConditions )
{
    const bool changed = std::memcmp( &InConditions, &m_settings.Conditions, sizeof( NetConditions ) ) != 0;
    m_settings.Conditions = InConditions;
    ApplyConditions();
    if( changed && m_hosting )
    {
        CLog::Log( CLog::LogType::Info, "PIE: conditions now " + Describe( InConditions ) );
    }
}


uint64_t NetPlay::SendControl( int InIndex, const std::string& InLine )
{
#if !USING( ME_PLATFORM_WINDOWS )
    for( std::unique_ptr<Child>& child : m_children )
    {
        if( child->Info.Index != InIndex || !child->Info.Running || child->StdinFd < 0 )
        {
            continue;
        }
        const uint64_t id = m_nextControlId++;
        const std::string line = std::to_string( id ) + " " + InLine + "\n";
        size_t written = 0;
        while( written < line.size() )
        {
            const ssize_t wrote = write( child->StdinFd, line.data() + written, line.size() - written );
            if( wrote <= 0 )
            {
                if( wrote < 0 && errno == EINTR )
                {
                    continue;
                }
                return 0;
            }
            written += static_cast<size_t>( wrote );
        }
        return id;
    }
#endif
    return 0;
}


bool NetPlay::TakeReply( int InIndex, uint64_t InId, std::string& OutOutput )
{
    for( std::unique_ptr<Child>& child : m_children )
    {
        if( child->Info.Index != InIndex )
        {
            continue;
        }
        PumpChild( *child );
        auto it = child->Replies.find( InId );
        if( it == child->Replies.end() )
        {
            return false;
        }
        OutOutput = std::move( it->second );
        child->Replies.erase( it );
        return true;
    }
    return false;
}


int NetPlay::CountChildProcesses()
{
#if USING( ME_PLATFORM_LINUX )
    const std::string self = std::to_string( getpid() );
    int count = 0;
    std::error_code error;
    std::filesystem::directory_iterator it( "/proc", error );
    // Processes come and go while it's read: an entry that went is skipped, never thrown.
    for( ; !error && it != std::filesystem::directory_iterator(); it.increment( error ) )
    {
        const std::filesystem::directory_entry& entry = *it;
        const std::string name = entry.path().filename().string();
        if( name.empty() || !std::all_of( name.begin(), name.end(), []( char c ) { return c >= '0' && c <= '9'; } ) )
        {
            continue;
        }
        // A zombie's cmdline is empty: gone, only not reaped yet.
        std::ifstream file( entry.path() / "cmdline", std::ios::binary );
        std::string cmdline( ( std::istreambuf_iterator<char>( file ) ), std::istreambuf_iterator<char>() );
        const std::string marker = std::string( "--pie-editor" ) + '\0' + self + '\0';
        if( cmdline.find( marker ) != std::string::npos )
        {
            ++count;
        }
    }
    return count;
#else
    return 0;
#endif
}


void NetPlay::DrawToolbar( bool InPlaying )
{
    const bool live = m_hosting;
    const std::string label = live ? "Net " + std::to_string( GetConnectedCount() ) + "/" + std::to_string( m_children.size() ) + "##NetPlay"
                                   : m_settings.Clients > 0 ? "Net x" + std::to_string( m_settings.Clients ) + "##NetPlay" : std::string( "Net##NetPlay" );
    if( ImGui::Button( label.c_str() ) )
    {
        ImGui::OpenPopup( "NetPlayPopup" );
    }
    ImGui::SetItemTooltip( "Play over the network (NET-7): a listen server in the editor and child client Players, with link conditions" );
    if( !ImGui::BeginPopup( "NetPlayPopup" ) )
    {
        return;
    }
    bool changed = false;
    ImGui::TextUnformatted( "Play In Editor over the network" );
    ImGui::Separator();
    ImGui::BeginDisabled( InPlaying );
    ImGui::SetNextItemWidth( ImGuiTheme::Em( 10.f ) );
    changed |= ImGui::SliderInt( "Clients", &m_settings.Clients, 0, kMaxClients, m_settings.Clients == 0 ? "offline" : "%d" );
    ImGui::SetItemTooltip( "0: Play is offline. Otherwise Play hosts in the editor and starts this many game Players that join it" );
    char args[512];
    std::snprintf( args, sizeof( args ), "%s", m_settings.ClientArgs.c_str() );
    ImGui::SetNextItemWidth( ImGuiTheme::Em( 16.f ) );
    if( ImGui::InputText( "Client arguments", args, sizeof( args ) ) )
    {
        m_settings.ClientArgs = args;
        changed = true;
    }
    ImGui::EndDisabled();
    changed |= ImGui::Checkbox( "Client logs in the Log", &m_settings.ShowClientLogs );
    ImGui::Separator();
    ImGui::TextUnformatted( live ? "Link conditions (live, both directions)" : "Link conditions (both directions)" );
    NetConditions conditions = m_settings.Conditions;
    float lossPercent = conditions.Loss * 100.f;
    float duplicatePercent = conditions.Duplicate * 100.f;
    bool conditionsChanged = false;
    ImGui::SetNextItemWidth( ImGuiTheme::Em( 10.f ) );
    conditionsChanged |= ImGui::SliderFloat( "Lag (ms, each way)", &conditions.LatencyMs, 0.f, 300.f, "%.0f" );
    ImGui::SetNextItemWidth( ImGuiTheme::Em( 10.f ) );
    conditionsChanged |= ImGui::SliderFloat( "Jitter (ms)", &conditions.JitterMs, 0.f, 100.f, "%.0f" );
    ImGui::SetNextItemWidth( ImGuiTheme::Em( 10.f ) );
    conditionsChanged |= ImGui::SliderFloat( "Loss (%)", &lossPercent, 0.f, 50.f, "%.1f" );
    ImGui::SetNextItemWidth( ImGuiTheme::Em( 10.f ) );
    conditionsChanged |= ImGui::SliderFloat( "Duplicates (%)", &duplicatePercent, 0.f, 50.f, "%.1f" );
    if( conditionsChanged )
    {
        conditions.Loss = lossPercent / 100.f;
        conditions.Duplicate = duplicatePercent / 100.f;
        SetConditions( conditions );
        changed = true;
    }
    if( !m_children.empty() )
    {
        ImGui::Separator();
        for( const std::unique_ptr<Child>& child : m_children )
        {
            const float rtt = GetRoundTripMs( child->Info.Index );
            if( child->Info.Running )
            {
                ImGui::Text( "Client %d: pid %d, %s", child->Info.Index, child->Info.Pid,
                    child->Info.ClientId > 0 ? ( "client #" + std::to_string( child->Info.ClientId ) + ( rtt >= 0.f ? ", " + std::to_string( static_cast<int>( rtt ) ) + " ms RTT" : "" ) ).c_str() : "connecting" );
            }
            else
            {
                ImGui::TextDisabled( "Client %d: exited (%d)", child->Info.Index, child->Info.ExitCode );
            }
            ImGui::SetItemTooltip( "%s", child->Info.LogPath.c_str() );
        }
    }
    if( !m_lastError.empty() )
    {
        ImGui::Separator();
        ImGui::TextWrapped( "Last error: %s", m_lastError.c_str() );
    }
    ImGui::EndPopup();
    if( changed )
    {
        SaveSettings();
    }
}

#endif
