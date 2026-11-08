#pragma once
#include "Dementia.h"
#include "Net/NetTransport.h"
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#if USING( ME_EDITOR )

class NetCore;
class UdpTransport;

// NET-7, Play In Editor over the network: with Clients above 0, Play hosts a listen server in the
// editor (its game world is the server's, its Game View the authoritative world; the engine's
// server has no local player) and starts that many child clients, the game Player of the same
// build (.build/Game_<platform>_<Config>/Player beside .build/Editor_<Config>/Havana, or
// --pie-player <path>), each joined with --connect to the editor on 127.0.0.1, the project and
// the scene being played (the saved file: a child loads it from disk). Children inherit the
// editor's environment, so an editor running headless (Tools/Headless.sh: a private Xvfb, software
// Vulkan) has headless children too, and nothing opens on the desktop.
//
// Conditions (lag, jitter, loss, duplicates) apply at the editor's end, both directions
// (ConditionedTransport::SetInboundConditions): lag is one way, so RTT is twice it, as --net-lag on
// every process would be. They change live, without touching the children. The editor's own
// --net-lag / --net-jitter / --net-loss / --net-seed are the starting values: each session seeds
// both directions from --net-seed (inbound its own sequence), live changes carry the sequences on.
//
// The server is set up as a Player --server's would be (NetLaunch::ApplySessionOptions and
// StartRecording): --net-timeout, --net-debug-clients, the replay ring killcams are cut from and
// --record-replay. The time scale stays x1 while it hosts (the clients' clocks run on wall time).
// Each child keeps its user state in <project>/.tmp/PIE/User<N>; an unattended editor's children
// are --transient (silent, nothing saved).
//
// Each child is driven over its stdin (--control-stdin: Engine/ControlChannel.h) and its stdout
// read back: its log goes to <project>/.tmp/PIE/Client<N>.log and, while ShowClientLogs is on,
// into the editor's log as "[client N] ..." (so an edscript's wait-log sees it), and its answers
// to SendControl come back by id. Stop closes the session (every child hears the Disconnect) and
// each child's stdin: a child quits when its stdin closes. One still running after 3 s gets
// SIGTERM, after 2 s more SIGKILL. A child dies with the editor however the editor ends: its
// stdin's other end goes with it, and on Linux PR_SET_PDEATHSIG kills it outright. Every child
// carries "--pie-editor <editor pid>", so CountChildProcesses can find this editor's in /proc (a
// sanity check after Stop; one left by an editor that died carries that editor's pid).
//
// Linux / POSIX only for now: elsewhere Play with clients reports that and stays offline.
class NetPlay
{
public:
    static constexpr int kMaxClients = 8;

    struct Settings
    {
        int Clients = 0;            // 0: Play is offline (the default)
        std::string ClientArgs;     // added to every child's command line
        NetConditions Conditions;   // at the editor's end, both directions
        bool ShowClientLogs = true;
    };

    struct ChildInfo
    {
        int Index = 0;              // 1..Clients, in start order
        int Pid = 0;
        int ClientId = 0;           // the server's id for it, once it said "connected as client #N"
        bool Running = false;
        int ExitCode = -1;
        std::string LogPath;
    };

    static NetPlay& Get();

    Settings& GetSettings() { return m_settings; }
    void LoadSettings();
    void SaveSettings() const;

    // The edscript's play-net: the next Play starts InClients children (with InExtraArgs too),
    // whatever the settings say.
    void SetPendingPlay( int InClients, const std::string& InExtraArgs );
    // Play (EditorApp::StartGame) asks before the world starts: a session wanted?
    bool IsWanted() const;
    // Hosts and starts the children (before the world starts: scripts' OnStart must see a server).
    // False with OutError when it couldn't (nothing is left running).
    bool Start( NetCore& InNet, uint16_t InTickRate, std::string& OutError );
    // Stop (EditorApp::StopGame, before the world goes): goodbyes, children told to quit (reaped by
    // Update, escalating to SIGTERM / SIGKILL).
    void Stop( NetCore& InNet );
    // Every frame (the editor's update): the children's output, their exits, the escalation, join and
    // leave lines.
    void Update();
    // The editor is going: whatever is left is stopped and waited for (bounded).
    void Shutdown();

    bool IsHosting() const { return m_hosting; }
    uint16_t GetPort() const { return m_port; }
    const std::string& GetLastError() const { return m_lastError; }
    // Clients the listen server has accepted (0 when not hosting).
    size_t GetConnectedCount() const;
    std::vector<ChildInfo> GetChildren() const;
    // The server's client id for child InIndex (1-based), 0 when it hasn't connected.
    int GetClientId( int InIndex ) const;
    // The listen server's round trip to child InIndex in ms, < 0 when unknown.
    float GetRoundTripMs( int InIndex ) const;

    // Live: applied at once while hosting, kept in the settings either way.
    void SetConditions( const NetConditions& InConditions );

    // A console line for child InIndex (--control-stdin): its id (0 when it can't be sent).
    uint64_t SendControl( int InIndex, const std::string& InLine );
    // Child InIndex's answer to InId, once it has come (the console output; taken once).
    bool TakeReply( int InIndex, uint64_t InId, std::string& OutOutput );

    // Processes on this machine started as this editor's PIE children that are still alive (zombies
    // aside): found in /proc by their --pie-editor <this editor's pid>, reaped or not.
    static int CountChildProcesses();

    // One child while hosting (Havana's Net Debugger panel; the edscript through it): Stop closes its
    // stdin as Stop does for all (the same escalation), Start runs it again with the arguments it
    // had (a new client id once it joins), Restart stops it and starts it again once it has gone.
    // False with OutError when it can't (not hosting, no such child, already running / stopped).
    bool StopChild( int InIndex, std::string& OutError );
    bool StartChild( int InIndex, std::string& OutError );
    bool RestartChild( int InIndex, std::string& OutError );

    // Havana's toolbar: the Net button and its popup (clients, client arguments, conditions; the
    // children and their links while playing).
    void DrawToolbar( bool InPlaying );
    // The popup's parts, which the Net Debugger panel draws too: the clients and their arguments
    // (disabled while playing), the link's condition sliders (live), and the children (with Stop /
    // Start / Restart buttons when InButtons). True when a setting changed (saved).
    bool DrawSettings( bool InPlaying );
    bool DrawConditions();
    void DrawChildren( bool InButtons );

    // Driving these widgets from an edscript (net-ui-set, net-ui-click) by their ImGui label:
    // queued, and taken by the next DrivenSlider / DrivenButton drawn with that label, as if the
    // user had dragged it to the value or clicked it. A queued label nothing draws stays queued.
    void QueueUi( const std::string& InLabel, const std::string& InValue );
    bool IsUiQueued( const std::string& InLabel ) const;
    // A queued action for InLabel, taken (for a widget drawn elsewhere: the panel's Export).
    bool TakeUi( const std::string& InLabel, std::string& OutValue );
    bool DrivenSliderFloat( const char* InLabel, float* InValue, float InMin, float InMax, const char* InFormat );
    bool DrivenSliderInt( const char* InLabel, int* InValue, int InMin, int InMax, const char* InFormat );
    bool DrivenButton( const char* InLabel );
    bool DrivenCheckbox( const char* InLabel, bool* InValue );

private:
    struct Child;
    NetPlay();
    ~NetPlay();
    bool SpawnChild( int InIndex, const std::vector<std::string>& InArgs, std::string& OutError );
    void PumpChild( Child& InChild );
    void HandleLine( Child& InChild, const std::string& InLine );
    void ReapChildren( double InNow );
    // Restart: children that have gone since StopChild, started again.
    void RestartPending();
    // InReseed: the session's start (the seeds); live changes keep the random sequences going.
    void ApplyConditions( bool InReseed = false );
    std::string FindPlayer() const;

    Settings m_settings;
    int m_pendingClients = -1;
    std::string m_pendingArgs;
    std::unique_ptr<UdpTransport> m_socket;
    std::unique_ptr<ConditionedTransport> m_conditioned;
    NetCore* m_net = nullptr;
    bool m_hosting = false;
    uint16_t m_port = 0;
    std::string m_lastError;
    std::vector<std::unique_ptr<Child>> m_children;
    uint64_t m_nextControlId = 1;
    uint64_t m_lastAccepted = 0;
    uint64_t m_lastLeft = 0;
    std::map<std::string, std::string> m_uiQueue;
};

#endif
