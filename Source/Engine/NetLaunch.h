#pragma once
#include "Net/NetCommands.h"
#include "Engine/NetDebuggerView.h"
#include "Net/NetDebug.h"
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
//   --net-report file.json   on exit: the session's summary (NetCore::WriteReport); a server's counts its
//                            snapshot bytes per entity for it (TopEntities)
//   --net-debugger           client, game-tools builds: the Net Debugger window (NET-7; the console's
//                            "net debugger" too): the link and its graphs, prediction, the misprediction
//                            timeline, each shot against the server's result, the top-bandwidth entities,
//                            world overlays. The console's "net view [tick]" answers the same as JSON
//                            (NetDebuggerView), which Havana's Net Debugger panel draws for each PIE child
//   --net-debug              client: what the Net Debugger shows (shots, bandwidth) without its window, in
//                            the report ("Shots", "TopEntities"); a server: count snapshot bytes per entity
//   --net-debug-clients      server, Release builds: take clients' Net Debugger subscriptions (shot results
//                            carry hitbox poses a client may not otherwise see; Debug and editor servers
//                            take them by default, Retail never)
//   --net-exit-when-empty    server: quit once its last client has gone (smoke tests)
//   --record-replay [file]   server: record the match (ME-316) to file as well as the ring (below); with
//                            neither --server nor --connect (single-player, offline practice), the session
//                            is recorded all the same (ME-215: NetCore::StartOfflineRecording)
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
//   --replay-overlays LIST --replay-heatmap [FILE] --replay-heatmap-layer LAYER[:GROUP] (ME-215)
//                            (and --replay-dump FILE --resim-check, which plays FILE to check it)
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

    // A session's options from the command line that every host and client takes, shared with the
    // editor's PIE listen server (NET-7) so it gets what a Player --server would: --net-timeout, and
    // on a host --net-debug-clients. Before Host / Connect.
    static void ApplySessionOptions( NetCore& InNet, bool InHost );
    // After Host: the replay ring killcams are cut from (ME-323; not with --no-replay-ring), and the
    // match replay's file too with --record-replay (ME-316). Logged either way. With no session and
    // InOfflineTickRate (the engine's fixed steps per second), --record-replay records the offline
    // session (ME-215).
    static void StartRecording( NetCore& InNet, uint16_t InOfflineTickRate = 0 );

    // The networked input actions' layout and the local player's command (NET-6) are the world's:
    // SingletonInput (ME-297). Start hands the session the game world's layout hash.

private:
    void ShowRole( const std::string& InRole );

    IWindow* m_window = nullptr;
    std::string m_title;
    std::unique_ptr<UdpTransport> m_socket;
    std::unique_ptr<ConditionedTransport> m_conditioned;
    std::string m_reportPath;
    // --replay: opens the file and starts the playback with the --replay-* options.
    bool StartReplay( NetCore& InNet );
    // The console's "replay" command (registered once).
    void RegisterReplayCommands( NetCore& InNet );
    // The console's "net" command (registered once): status, debugger, mispredicts, export.
    void RegisterNetCommands( NetCore& InNet );
    // The Net Debugger's state (NetDebuggerView.h): open, what its window keeps (the mark picked,
    // the overlays), the link's rate over the last second (sampled every frame from the
    // connection's byte counts) and its graphs' history; when a tool last asked for the view
    // ("net view", Havana's Net Debugger panel over --control-stdin).
    struct NetDebuggerState
    {
        bool Open = false;
        NetDebuggerUI UI;
        NetRateMeter Rate;
        NetDebuggerHistory History;
        double ViewedAt = -1.0;
    } m_netDebugger;
    // A tool asked for the view in the last kViewedSeconds: the session keeps asking the server for
    // the shots and bandwidth, and the overlays draw, as with the window open.
    static constexpr double kViewedSeconds = 3.0;
    bool IsViewed() const;
    // --net-debug: the Net Debugger's data (shots, bandwidth) without its window.
    bool m_netDebug = false;
    // What the session should ask for / count (NET-7): the Net Debugger's, --net-debug, a server's report.
    NetDebugSubscription WantedDebug( NetCore& InNet ) const;
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
