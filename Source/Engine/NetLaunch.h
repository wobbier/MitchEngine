#pragma once
#include "Net/NetCommands.h"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class Input;
class IWindow;
class NetCore;
class UdpTransport;
class ConditionedTransport;

// Networking from the command line (NET-5): Engine::Run starts it once the scene has loaded (the
// handshake hashes the scene's networked entities), checks it every frame and ends it on exit.
//
//   --server [port]          host on UDP port (7777 when omitted, 0 = any free port), every interface;
//                            a headless Server build hosts without it too (ME-308), unless it connects
//   --connect host:port      join a server (IPv4 address or name)
//   --net-lag ms             hold this side's packets back (one way; both sides: double it for RTT)
//   --net-jitter ms          +/- around the lag
//   --net-loss f             drop this fraction of this side's packets (0.05, or 5%)
//   --net-seed n             the conditions' random seed (repeatable runs)
//   --net-timeout s          seconds of silence before a peer counts as gone (default 10)
//   --net-report file.json   on exit: the session's summary (NetCore::WriteReport)
//   --net-exit-when-empty    server: quit once its last client has gone (smoke tests)
//   --record-replay [file]   server: record the match (ME-316) to file (user://Replays/<UTC time>.mreplay
//                            when omitted; user:// paths go to the user folder, a bare file name to
//                            user://Replays/, other relative paths to the working directory). Read it with --replay-dump
//
// Match replay playback (ME-316 phase 2), in a client build (the Player), with no session:
//   --replay file            play a replay into the loaded scene (it must be the scene it was recorded on:
//                            --scene when it isn't the startup one); user:// paths and bare names found
//                            under user://Replays/ work too. Plays from the first tick
//   --replay-start-tick N    start at tick N
//   --replay-paused          start paused
//   --replay-pause-at N      pause when playing reaches tick N
//   --replay-speed X         0.1 to 4
//   --replay-loop A:B        play ticks A to B over and over
//   --replay-check           once paused at --replay-pause-at (or the start, paused), check the world
//                            against the recording (NetCore::CheckPlayback), log every entity, and quit:
//                            exit code 1 when anything differs (headless tests)
// The console's "replay" command (play, pause, step, seek, speed, loop, pause-at, check, info) drives it
// too (the edscript "console replay ..." and --console "replay ..."), and game-tools builds draw a
// Replay window (Space play / pause, Left / Right step, Shift for 10).
class NetLaunch
{
public:
    NetLaunch();
    ~NetLaunch();

    // True when the command line asks for a session (whether or not it started). A headless Server
    // build always does: it hosts unless told to connect.
    static bool IsRequested();
    // The command line asks to host (or this is a headless build that wasn't told to connect).
    static bool IsHosting();
    // The command line asked for a session that couldn't start (the process exits non-zero).
    bool HasFailed() const { return !m_error.empty(); }

    // Starts what the command line asks for: false when it asked and couldn't (logged; Update then
    // says to quit, and the report says why). InTickRate: the engine's fixed steps per second.
    // The window's title (InTitle) gains the session's role, so local test windows tell apart.
    bool Start( NetCore& InNet, uint16_t InTickRate, IWindow* InWindow = nullptr, const std::string& InTitle = "" );
    // Every frame: logs how the session is going; true when the engine should quit.
    bool Update( NetCore& InNet );
    // Before the engine shuts down: the report, goodbyes, the session ends.
    void Shutdown( NetCore& InNet, double InNow );

    // --replay was given (ME-316).
    static bool IsReplaying();
    // Game-tools builds with ImGui: the Replay window while a replay plays.
    void DrawReplayControls( NetCore& InNet );

    // Networked input actions (NET-6, InputAction::Networked), sorted by name: buttons take the
    // command's bits in that order, Value / Vector2 actions its 4 axes. Built before Start.
    void BuildCommandLayout( const Input& InInput );
    // Without a session: builds the layout again if the action map changed since (a hot reload,
    // the editor loading another map). A session's layout is fixed (the handshake hashes it).
    void RefreshCommandLayout( const Input& InInput );
    // Changes whenever the layout is built (scripts cache action indices against it).
    uint32_t GetLayoutVersion() const { return m_layoutVersion; }
    // The local player's command for the coming tick, plus whatever scripts hold (Net.Press).
    void SampleCommand( const Input& InInput, InputCommand& OutCommand ) const;
    // An action's button bit, or -1 (not networked, or not a button).
    int GetActionIndex( const std::string& InAction ) const;
    // A networked Value / Vector2 action's first axis, or -1 (not networked, or a button);
    // OutCount: its axes (1 or 2).
    int GetAxisIndex( const std::string& InAction, int* OutCount = nullptr ) const;
    // The local player's command for the fixed step being simulated, as the engine sampled it
    // (C#'s Net.LocalCommand): recorded every step while HasCommandLayout(), offline and on a
    // client, with its axes as the server would read them. Offline, Tick counts the steps.
    void RecordLocalCommand( const InputCommand& InCommand );
    // A step that samples nothing (no layout, or the server): Net.LocalCommand reads invalid, not
    // the last command recorded.
    void ClearLocalCommand();
    bool HasLocalCommand() const { return m_hasLocalCommand; }
    const InputCommand& GetLocalCommand() const { return m_localCommand; }
    const InputCommand& GetPreviousLocalCommand() const { return m_previousLocalCommand; }
    void SetActionOverride( int InIndex, bool InHeld );
    // The local player's absolute aim (NET-15, C#'s Net.SetAim), sent with every command until
    // cleared: degrees, InputCommand's convention.
    void SetAim( float InYaw, float InPitch );
    void ClearAim() { m_hasAim = false; }
    // The editor's Stop: what scripts held (Net.Press) and aimed (Net.SetAim), and the recorded
    // local commands, go with the play session, so the next Play starts with none. The layout
    // stays (scripts' cached indices are keyed on its version).
    void ResetLocalInput();
    // Whether any action is networked: then the engine samples the local command every tick
    // (otherwise a C++ game's own NetCore::SetLocalCommand stands).
    bool HasCommandLayout() const { return !m_commandSlots.empty() || m_overrides != 0 || m_hasAim; }
    // The layout as a hash, for the handshake.
    uint64_t GetCommandSchema() const { return m_commandSchema; }

private:
    void ShowRole( const std::string& InRole );

    IWindow* m_window = nullptr;
    std::string m_title;
    std::unique_ptr<UdpTransport> m_socket;
    std::unique_ptr<ConditionedTransport> m_conditioned;
    std::string m_reportPath;
    // Starts the match replay if the command line asks (a server; logged either way).
    void StartRecording( NetCore& InNet );
    // --replay: opens the file and starts the playback with the --replay-* options.
    bool StartReplay( NetCore& InNet );
    // The console's "replay" command (registered once).
    void RegisterReplayCommands( NetCore& InNet );
    // --replay-check, once the playhead rests where it was asked to: false when the world differs.
    bool RunReplayCheck( NetCore& InNet );
    bool m_replayCheck = false;
    int m_replayCheckWait = 0;          // frames paused on the tick before checking (spawns settle)
    bool m_replayChecked = false;
    std::string m_error;
    bool m_requested = false;
    bool m_exitWhenEmpty = false;
    struct CommandSlot
    {
        std::string Action;
        int Index = 0;          // button bit, or first axis
        bool IsButton = true;
        int AxisCount = 0;      // 1 (Value) or 2 (Vector2)
    };
    std::vector<CommandSlot> m_commandSlots;
    uint32_t m_layoutVersion = 0;
    uint32_t m_layoutMapVersion = 0;    // the action map's version it was built from
    InputCommand m_localCommand;
    InputCommand m_previousLocalCommand;
    uint32_t m_localTick = 0;
    bool m_hasLocalCommand = false;
    uint32_t m_overrides = 0;
    bool m_hasAim = false;
    float m_yaw = 0.f;
    float m_pitch = 0.f;
    uint64_t m_commandSchema = 0;
    // What was last logged, so changes log once.
    int m_lastState = -1;
    uint64_t m_lastAccepted = 0;
    uint64_t m_lastDisconnects = 0;
    uint64_t m_lastTimeouts = 0;
    uint64_t m_lastRejected = 0;
};
