#pragma once
#include "ECS/EntityHandle.h"
#include "Engine/KillcamPlayer.h"
#include "Math/Vector3.h"
#include "Net/NetReplay.h"
#include "Net/NetReplayResim.h"
#include "Net/NetReplayTimeline.h"
#include "JSON.h"
#include <atomic>
#include <cstdint>
#include <deque>
#include <future>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

class NetCore;

// The match replay viewer's debug tools (ME-316 phase 3), on top of the Player's playback
// (NetLaunch, --replay). NetLaunch owns one while a replay plays.
//
// - Timeline: the replay's markers and each client's link (NetReplayTimeline), built on the
//   background lane from a file source of its own when the replay opens. Game-tools builds draw it
//   as a scrubber with the markers on it (click one to jump there) and the clients' RTT, slack and
//   missed commands graphed along it.
// - Inspector at the tick shown: each networked entity's recorded pose, its C++ Replicated fields by
//   name (NetSchema::Describe of the recorded bytes), its C# [Replicated] bytes and public script
//   fields as the world has them (HP, ammo...), a character's mover and [Predicted] bytes, each
//   against the tick before (what changed is highlighted, and listed in the report).
// - Input: each client's command this tick, buttons and axes by the networked actions' names.
// - Lag compensation: each shot this tick, its ray, the hitboxes as it was judged against them
//   (rewound), the same hitboxes in the world at the tick (live), how far the rewind moved them,
//   and the hit point. Drawn in the world (DebugDraw) with trails of the characters' last 2 s and
//   every live hitbox.
// - Camera: the game's, free (WASD / QE, right mouse to look), following an entity, or a player's
//   own eyes from its recorded aim.
// - Resim check (NetCore::ResimulateReplay) from the inspector, the console or the command line.
//
// Command line (with --replay; --replay-dump FILE --resim-check works too, as --replay FILE):
//   --replay-marker KIND[:N]   once the timeline is built, rest on the Nth marker of KIND (kill, join,
//                              leave, event, hit, miss, resync, hitch; N from 0, negative from the end)
//   --replay-report FILE       the viewer's view of the tick it rests on as JSON (inspector, input,
//                              shots with rewound and live hitboxes, the marker): written when
//                              --replay-check checks, or once it rests there without one
//   --resim-check [A:B]        the resim check for every player's character over A..B (all of it by
//                              default), logged ("Resim: ..."), then quit: exit 0 when every one
//                              matches, 3 when a tick diverges, 1 when any couldn't be checked (or
//                              the run ended before the check did: NetLaunch::Shutdown)
//   --replay-camera MODE       game, free, follow:NETID or pov:NETID
//   --replay-overlays LIST     what to draw in the world: shots,hitboxes,trails (default all; none)
class ReplayViewer
{
public:
    enum class CameraMode : uint8_t
    {
        Game,
        Free,
        Follow,
        Pov,
    };

    // A networked action, as the command layout has it (NetLaunch's): buttons are bits, the rest axes.
    struct Action
    {
        std::string Name;
        int Index = 0;
        bool IsButton = true;
        int AxisCount = 0;
    };

    ReplayViewer( std::string InPath, std::vector<Action> InActions );
    ~ReplayViewer();
    ReplayViewer( const ReplayViewer& ) = delete;
    ReplayViewer& operator=( const ReplayViewer& ) = delete;

    // The command line's options; false with OutError on a bad one. Starts the timeline's build.
    bool Configure( NetCore& InNet, std::string& OutError );
    // Every frame, after the playback moved (NetLaunch::Update): the timeline, a marker jump, the
    // resim check's steps, the inspector's cache, the world overlays. True when the command line's
    // work is done and the run should end (OutError: why it failed, if it did).
    bool Update( NetCore& InNet, std::string& OutError );
    // A marker jump is still on its way (--replay-check waits for it).
    bool IsSettling() const;
    // --replay-check ran: the report, if one was asked for, is written now. False when it couldn't be.
    bool OnChecked( NetCore& InNet, std::string& OutError );
    // The exit code its own work asks for (the resim check's), -1 for none.
    int GetExitCode() const { return m_exitCode; }

    // Game-tools builds: the Replay, Inspector, Input and Shots windows.
    void DrawUI( NetCore& InNet );
    // Before the cameras update: places the current camera (free, follow, POV).
    void UpdateCamera( NetCore& InNet, float InDeltaSeconds );
    // The console's "replay" verbs it knows (marker, next, prev, markers, inspect, input, shots,
    // resim, camera, overlay, report); OutHandled false for any other.
    std::string RunCommand( NetCore& InNet, const std::vector<std::string>& InArgs, bool& OutHandled );

    // The viewer's view of the tick shown, as the report has it.
    json MakeReport( NetCore& InNet );
    bool WriteReport( NetCore& InNet, const std::string& InPath, std::string& OutError );

    // Null until the background build has finished.
    const NetReplayTimeline* GetTimeline() const { return m_timelineReady ? m_timeline.get() : nullptr; }
    void SetCamera( CameraMode InMode, uint32_t InNetId = 0 );

private:
    struct Shown;
    struct HitboxView
    {
        uint32_t NetId = 0;
        std::string Name;
        uint8_t Shape = 0;
        Vector3 Position;
        float Radius = 0.f;
        float HalfHeight = 0.f;
        Vector3 HalfExtents;
        Vector3 Up;     // the capsule's axis (rotation * up)
        Quaternion Rotation;
        bool Occluder = false;
    };

    void PollTimeline();
    // Rest on InTick, paused, having shown the tick before first (the inspector's diffs).
    void JumpTo( NetCore& InNet, uint32_t InTick );
    bool JumpToMarker( NetCore& InNet, NetReplayMarker::Kind InKind, int InNth, std::string& OutError );
    // The shown tick's record and the one before, and the world's view of them, when the tick changed.
    void Refresh( NetCore& InNet );
    std::vector<HitboxView> LiveHitboxes( NetCore& InNet ) const;
    void DrawOverlays( NetCore& InNet );
    void StepResimCheck( NetCore& InNet, std::string& OutError, bool& OutDone );
    std::string ResimText( const NetReplayResim::Result& InResult ) const;
    std::string CommandText( const InputCommand& InCommand ) const;
    std::string Label( uint32_t InNetId ) const;
#if USING( ME_IMGUI ) && !USING( ME_EDITOR ) && USING( ME_GAME_TOOLS )
    void DrawReplayWindow( NetCore& InNet );
    void DrawTimeline( NetCore& InNet );
    void DrawInspector( NetCore& InNet );
    void DrawInput( NetCore& InNet );
    void DrawShots( NetCore& InNet );
#endif

    std::string m_path;
    std::vector<Action> m_actions;
    // The timeline, built on the background lane from a source of its own.
    std::unique_ptr<NetReplayTimeline> m_timeline;
    std::future<std::string> m_timelineBuild;
    std::atomic<bool> m_cancel{ false };
    bool m_timelineReady = false;
    std::string m_timelineError;
    // A reader of its own (the tick before, trails, resim spans): the playback keeps its chunk.
    std::unique_ptr<NetReplayFileSource> m_source;
    std::unique_ptr<NetReplayReader> m_reader;
    // The POV camera's (ME-343): a player's eye and aim from its tick whose view tick is the tick shown (what its
    // screen had then, as a killcam shows it), read ahead of the playhead by a reader of their own.
    std::unique_ptr<NetReplayFileSource> m_povSource;
    std::unique_ptr<NetReplayReader> m_povReader;
    std::vector<KillcamPlayer::KillerSample> m_povSamples;
    uint32_t m_povTarget = 0;
    uint8_t m_povClient = 0;
    // Its own meshes, models and text, hidden while it's the POV (its eye is ahead of its body by that delay):
    // shown again when the camera leaves it.
    std::vector<EntityHandle> m_povHidden;
    void HidePovTarget( Transform& InBody );
    void ShowPovTarget();

    // The tick shown and the one before it, as recorded and as the world has them.
    std::unique_ptr<Shown> m_shown;
    std::unique_ptr<Shown> m_before;
    uint32_t m_selected = 0;        // the inspector's entity

    // Trails: each character's recorded positions over the last kTrailTicks.
    std::map<uint32_t, std::deque<std::pair<uint32_t, Vector3>>> m_trails;
    uint32_t m_trailTick = 0;
    bool m_hasTrails = false;
    bool m_drawShots = true;
    bool m_drawHitboxes = true;
    bool m_drawTrails = true;
    int m_selectedShot = -1;        // -1: all

    // Marker jump: waiting for the timeline (--replay-marker), then the tick before, then the tick.
    std::string m_markerRequest;    // "kill:-1"
    int m_jumpStage = 0;            // 0 none, 1 showing the tick before, 2 stepping onto it, 3 settling
    uint32_t m_jumpTick = 0;
    int m_jumpFrames = 0;
    int m_jumpMarker = -1;          // the marker jumped to (the report names it)
    uint32_t m_markerMask = ~0u;    // the kinds the scrubber shows and next / prev visit

    // --replay-report
    std::string m_reportPath;
    bool m_reportWritten = false;
    bool m_checkPending = false;    // --replay-check will ask for the report

    // --resim-check
    bool m_resimCheck = false;
    uint32_t m_resimFrom = 0;
    uint32_t m_resimTo = 0xFFFFFFFFu;
    std::vector<std::pair<uint32_t, std::pair<uint32_t, uint32_t>>> m_resimQueue;   // NetId, its span
    bool m_resimQueued = false;
    int m_resimFrames = 0;
    std::vector<NetReplayResim::Result> m_resimResults;
    std::string m_lastResim;        // the inspector's last result
    int m_exitCode = -1;

    // Camera
    CameraMode m_camera = CameraMode::Game;
    uint32_t m_cameraTarget = 0;
    Vector3 m_freePosition;
    float m_freeYaw = 0.f;
    float m_freePitch = 0.f;
    bool m_freePlaced = false;
};
