#pragma once
#include "Net/NetCommands.h"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class IWindow;
class NetCore;
class UdpTransport;
class ConditionedTransport;
class ReplayViewer;

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
//   --record-replay [file]   server: record the match (ME-316) to file as well as the ring (below)
//                            (user://Replays/<UTC time>.mreplay when omitted; user:// paths go to the user folder,
//                            a bare file name to user://Replays/, other relative paths to the working directory).
//                            Read it with --replay-dump
//   --no-replay-ring         server: don't keep the 12 s replay ring every server keeps by default for killcams
//                            (ME-323: Net.SendKillcam cuts them from it; while it's kept, Replay.IsRecording is
//                            true and Replay.Event moments go into it). --record-replay keeps it whatever this says
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
//
// The viewer's debug tools (ME-316 phase 3, ReplayViewer.h): a timeline with markers, an inspector,
// input, lag-compensation and resim views, world overlays and cameras; their options:
//   --replay-marker KIND[:N] --replay-report FILE --resim-check [A:B] --replay-camera MODE
//   --replay-overlays LIST   (and --replay-dump FILE --resim-check, which plays FILE to check it)
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
    // The process's exit code: 1 when it failed, a replay check's own (--resim-check: 3 when a tick
    // diverged), else 0.
    int GetExitCode() const;

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
    // Game-tools builds with ImGui: the replay viewer's windows while a replay plays.
    void DrawReplayControls( NetCore& InNet );
    // Before the cameras update: the replay viewer's free / follow / POV camera.
    void UpdateReplayCamera( NetCore& InNet, float InDeltaSeconds );
    // Game-tools builds with ImGui: the Net Debugger window (NET-7) for a client session, opened by
    // --net-debugger or the console's "net debugger".
    void DrawNetDebugger( NetCore& InNet );

    // The networked input actions' layout and the local player's command (NET-6) are the world's:
    // SingletonInput (ME-297). Start hands the session the game world's layout hash.

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
    // The console's "net" command (registered once): status, debugger, mispredicts, export.
    void RegisterNetCommands( NetCore& InNet );
    // The Net Debugger's state: open, the mark picked on its timeline, the link's rate over the
    // last second (sampled from the connection's byte counts).
    struct NetDebuggerState
    {
        bool Open = false;
        bool HasSelected = false;
        uint32_t Selected = 0;
        std::string LastExport;
        double RateAt = -1.0;
        uint64_t BytesIn = 0;
        uint64_t BytesOut = 0;
        double KbpsIn = 0.0;
        double KbpsOut = 0.0;
    } m_netDebugger;
    // --replay-check, once the playhead rests where it was asked to: false when the world differs.
    bool RunReplayCheck( NetCore& InNet );
    // The viewer's tools (ME-316 phase 3), while a replay plays.
    std::unique_ptr<ReplayViewer> m_viewer;
    int m_viewerExitCode = -1;          // the viewer's, kept past Shutdown
    bool m_replayCheck = false;
    int m_replayCheckWait = 0;          // frames paused on the tick before checking (spawns settle)
    bool m_replayChecked = false;
    std::string m_error;
    bool m_requested = false;
    bool m_exitWhenEmpty = false;
    // What was last logged, so changes log once.
    int m_lastState = -1;
    uint64_t m_lastAccepted = 0;
    uint64_t m_lastDisconnects = 0;
    uint64_t m_lastTimeouts = 0;
    uint64_t m_lastRejected = 0;
};
