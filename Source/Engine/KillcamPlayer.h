#pragma once
#include "Events/Event.h"
#include "Math/Vector3.h"
#include "Pointers.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

class BaseCore;
class NetCore;
class NetKillcamReceiver;
class NetKillcamSource;
class NetReplayReader;
struct NetReplayTick;
class World;

// A killcam started or ended on the victim's side (ME-323 phase 3). Fired by the KillcamPlayer on the
// main thread, after the live world's late update; the live world's ScriptCore hands it to C# (EventBus
// KillcamStarted / KillcamEnded, and Net.Killcam.Started / Ended).
class KillcamEvent
    : public Event<KillcamEvent>
{
public:
    enum class Kind : uint8_t
    {
        Started,
        Ended,
    };
    Kind Type = Kind::Started;
    uint32_t Id = 0;            // the server's killcam id
    uint32_t KillerNetId = 0;
    uint32_t VictimNetId = 0;
    // Ended: "played" (to the death tick and the hold after it), "skipped" (Skip), "replaced" (a newer
    // killcam started), "stopped" (the session ended, a scene loaded, the engine shut down) or
    // "failed: <why>" (the replay world couldn't be made, the stream stalled).
    std::string Reason;
    // The live world: the one whose session received the killcam.
    const World* Source = nullptr;
};


// Plays killcams into a replay world of their own (ME-323 phase 3, NET-17).
//
// The live world's NetCore receives a killcam (NetKillcamReceiver: the server's ME-316 chunks of the
// killer's last seconds, streaming in). Once its first chunk is in, the player plays it into a second
// World with its own NetCore (and, through Populate, the scene and whatever cores present it: the
// engine's camera, render and script cores). That world is built once (Prepare, ahead of the first
// death, or at the first killcam) and kept between killcams (ME-343): after each one, what the
// killcam spawned goes and the world is as it was built, ready for the next (a world that can't be
// made whole again, a placed entity destroyed, is built afresh). It plays the stream
// (NetCore::StartPlayback over a NetKillcamSource): nothing is predicted, every entity follows the
// recording, interpolated, and the playback waits on the last tick there is while chunks are still
// coming (NetReplayPlayback::SetStreaming). The live world isn't touched: it keeps receiving,
// predicting and simulating, and its session never waits on the killcam.
//
// Each frame Update moves the playhead, poses the replay world, and puts its main camera at the
// killer's eye looking along the killer's recorded aim (its client's commands, interpolated between
// ticks; a bot's facing), with the killer's own meshes hidden (first person). It plays on past the
// death to the stream's last tick when the server sent the seconds after it (NetCore::SendKillcam's
// InAfterSeconds), the killer's view carrying on from its commands then. It ends once that last tick
// has shown for HoldSeconds (HoldAfterSeconds for one that ran on past the death), on Skip, when a newer killcam replaces it, when the session ends
// (Stop), or when the stream stalls; then the replay world is reset at once (Release destroys it: a
// scene load, the session's end). One that ends before its stream is whole stops the download
// (NetCore::CancelKillcamDownload, ME-343).
//
// Presentation (which world the renderer, the audio listener and the screen's camera show) is the
// owner's: Engine::Run presents the replay world while IsPlaying, the live world otherwise.
//
// No engine globals: tests drive it on NetHarness worlds.
class KillcamPlayer
{
public:
    struct Settings
    {
        // Play each new killcam as soon as its first chunk is in.
        bool AutoPlay = true;
        // Seconds the last tick stays on screen before the switch back: the death's, when the stream ends on it.
        double HoldSeconds = 1.0;
        // The same for a stream that runs on past the death: its last frame isn't the kill, so it barely holds.
        double HoldAfterSeconds = 0.2;
        // Seconds the playback may wait on a chunk that doesn't come before it gives up.
        double MaxStallSeconds = 3.0;
        // The replay world's main camera at the killer's eye along its recorded aim.
        bool KillerView = true;
        // The eye above the killer's origin; < 0: the live world's SingletonKillcam::EyeHeight (C#'s
        // Net.Killcam.EyeHeight: where the game puts its eye), else its CharacterController's top less 0.15 m
        // (1.6 m without one).
        float EyeHeight = -1.f;
        // The killer's own meshes, models and text hidden in the killer's view.
        bool HideKiller = true;
        // The replay world kept between killcams, reset after each (ME-343); off: built for each one and
        // destroyed after it (ME-323's way).
        bool KeepWorld = true;
    };

    // What a replay world is: its cores (declared first: they outlive the world, which is destroyed
    // first) and the world. Populate adds to both.
    struct ReplayWorld
    {
        std::vector<std::unique_ptr<BaseCore>> Cores;
        NetCore* Net = nullptr;     // in Cores, added to the world before Populate
        SharedPtr<World> Instance;
        ~ReplayWorld();
    };
    // Builds a replay world's contents before the playback starts: the live scene's placed entities (the
    // playback refuses a world whose placed networked entities differ), presentation cores, the NetCore's
    // script glue. The world is already flagged IsReplayWorld and has its NetCore and SingletonKillcam;
    // the player starts it afterwards. False with OutError: the killcam fails.
    using Populate = std::function<bool( ReplayWorld& InWorld, std::string& OutError )>;

    // The killer's view (ME-343): the net aim convention (yaw about +Y, 0 looking down +Z and 90 down +X;
    // pitch up from level, in degrees) as a direction, and where a world point lands in a view from InEye
    // along that aim with a vertical field of view (degrees) and a width / height aspect: X and Y run -1..1
    // across the frame (X right, Y up), Depth along the view; Inside when it's in front and in the frame.
    struct ViewPoint
    {
        float X = 0.f;
        float Y = 0.f;
        float Depth = 0.f;
        bool Inside = false;
    };
    static Vector3 AimDirection( float InYawDegrees, float InPitchDegrees );
    static ViewPoint Project( const Vector3& InEye, float InYawDegrees, float InPitchDegrees, float InFieldOfView, float InAspect, const Vector3& InPoint );

    // What the killer saw (ME-343). A client shows the others at its view tick (interpolated, some ticks
    // behind the tick it predicts its own pawn at and aims in; its commands carry it, and lag compensation
    // rewinds shots to it), so the world at a time T was on the killer's screen while its client was at the
    // tick whose command's view tick reached T: its eye and aim then are the killer's view of T.
    // A killer's tick: its client's command (aim, view tick) and its pawn's recorded position.
    struct KillerSample
    {
        uint32_t Tick = 0;
        bool HasAim = false;
        float Yaw = 0.f;
        float Pitch = 0.f;
        double ViewTick = -1.0;     // < 0: none (an old recording's)
        bool HasPosition = false;   // the killer is in the tick's state
        Vector3 Position;
    };
    struct KillerView
    {
        double Tick = 0.0;          // the killer's tick (fractional: between two)
        double ViewTick = 0.0;      // the world tick it was looking at (InTime, unless InTime is outside the samples' view ticks)
        bool HasAim = false;
        float Yaw = 0.f;
        float Pitch = 0.f;
        bool HasPosition = false;
        Vector3 Position;
    };
    // The killer's view of world time InTime from InSamples (consecutive ticks, ascending): between the first
    // sample whose view tick reaches InTime and the one before it (by view tick; aim the short way round,
    // positions straight); a sample without a view tick counts as its own tick. Before the first sample's view
    // tick: the first sample; past the last one's: the last (the killer's last command, the kill).
    static KillerView FindKillerView( const std::vector<KillerSample>& InSamples, double InTime );
    // InTick as a KillerSample for the killer InKillerNetId of client InClient.
    static KillerSample MakeKillerSample( const NetReplayTick& InTick, uint8_t InClient, uint32_t InKillerNetId );
    // InOutSamples for FindKillerView( InOutSamples, InTime ): from the tick before InTime as far as the first tick
    // whose view tick passes it (its client's interpolation delay ahead), no further than InLast, what InReader has,
    // or two seconds at InTickRate; ticks before InFirst aren't read. Kept between calls while InTime moves on (a
    // move back reads again). The killcam's read-ahead, and the replay viewer's POV camera.
    static void ReadKillerSamples( NetReplayReader& InReader, uint8_t InClient, uint32_t InKillerNetId, uint32_t InFirst, uint32_t InLast, uint16_t InTickRate, double InTime,
        std::vector<KillerSample>& InOutSamples );

    KillcamPlayer() = default;
    ~KillcamPlayer();
    KillcamPlayer( const KillcamPlayer& ) = delete;
    KillcamPlayer& operator=( const KillcamPlayer& ) = delete;

    void SetPopulate( Populate InPopulate ) { m_populate = std::move( InPopulate ); }
    // Builds the replay world now, for InLive's killcams, if there's none yet (ME-343): ahead of the first
    // death, so that one doesn't pay for it (Engine: once its scene's read ahead). It builds (or tries) once
    // until Release: a world thrown away later (it lost a placed entity, or didn't fit a stream), or a build
    // that failed, is left to the next killcam, so live play never pays for a build. True when the world is
    // there.
    bool Prepare( World& InLive );
    // Ends whatever plays ("stopped") and destroys the replay world: a scene load (the next scene's
    // killcams need its world), the session's end, shutdown.
    void Release();
    Settings& GetSettings() { return m_settings; }

    // Every frame, once the live world's frame is simulated (Engine::Run: after its late update, before
    // presentation): starts a killcam that has arrived, moves the one playing by InFrameSeconds, places
    // the camera, and ends it when it's done. InLive: the world whose NetCore receives killcams.
    void Update( World& InLive, double InFrameSeconds );
    // Ends the killcam playing (C#'s Net.Killcam.Skip) at the start of the next Update, which in the
    // engine is later the same frame for a live world's script: the replay world goes then (never while
    // its own scripts are running, which may be what asked). With none playing, it lets the newest killcam
    // received go before it plays (ME-343: one still on its way, its first chunk not in or AutoPlay off): it
    // never plays and its download stops. Nothing to skip: nothing happens.
    void Skip() { m_skipRequested = true; }
    // Ends whatever plays ("stopped"): a scene load, the session's end, shutdown.
    void Stop();

    bool IsPlaying() const { return m_playing; }
    // The replay world while one plays (null otherwise).
    World* GetWorld() const;
    SharedPtr<World> GetWorldPtr() const;
    NetCore* GetNet() const { return m_playing ? m_replay->Net : nullptr; }
    // The replay world, playing or kept between killcams (null before it's built and after Release). The
    // owner keeps a kept one off screen (its cameras and RenderCore not presented).
    World* GetKeptWorld() const { return m_replay ? m_replay->Instance.get() : nullptr; }

    // The killcam playing, or the last one played.
    struct Status
    {
        uint32_t Id = 0;
        uint32_t KillerNetId = 0;
        uint32_t VictimNetId = 0;
        uint8_t KillerClient = 0;   // 0: a bot's
        uint32_t FirstTick = 0;
        uint32_t DeathTick = 0;
        uint32_t LastTick = 0;      // the stream's: DeathTick, or the last of the seconds after it
        uint16_t TickRate = 60;
        // ME-343: it played in the kept replay world (reset after the last), not one built for it; and the
        // milliseconds to get the world ready (building it, or starting the playback in the kept one).
        bool WorldKept = false;
        double ReadyMs = 0.0;
        double Tick = 0.0;          // the tick shown (fractional between ticks)
        double Elapsed = 0.0;       // seconds since it started
        double Duration = 0.0;      // seconds it plays: first tick to the last at its tick rate, then the hold
        bool Complete = false;      // every chunk is in
        // The replay world's main camera was put at the killer's eye this frame (KillerView, and the world
        // has a camera entity to move), for logs and captures. Eye / Yaw / Pitch: the killer's view,
        // placed or not.
        bool HasView = false;
        // The killer's tick the view is from (ME-343): ahead of Tick by its client's interpolation delay (the
        // killer's command whose view tick is Tick), Tick for a bot's; it reaches DeathTick at the kill.
        double KillerTick = 0.0;
        // The world tick that view was looking at: Tick, except while the killer's tick is held at the kill
        // (from its view tick to the death) or before the stream's first view tick.
        double KillerViewTick = 0.0;
        Vector3 KillerAt;           // the killer's origin then
        Vector3 Eye;
        float Yaw = 0.f;
        float Pitch = 0.f;
        // The victim in that view (ME-343), while the replay world has it: its origin, and where that lands
        // in the frame (the main camera's field of view and aspect).
        bool HasVictim = false;
        Vector3 VictimAt;
        ViewPoint VictimView;
        // The killer's own shot at the death tick nearest its aim (ME-343; not a projectile's), once the view
        // reaches the kill: where the game fired from and along, against the eye and aim the view has.
        bool HasShot = false;
        Vector3 ShotOrigin;
        Vector3 ShotDirection;
    };
    const Status& GetStatus() const { return m_status; }
    // The killer's view this frame as a log line's words: "the camera at killer #k's eye (x, y, z) aiming yaw Y
    // pitch P, the killer at (x, y, z), the victim #v at (x, y, z) on screen at (x, y) N m away" (or "off screen",
    // "behind the camera", "not in the world"); empty without a view.
    std::string DescribeView() const;

    struct Stats
    {
        uint64_t Started = 0;
        uint64_t Played = 0;
        uint64_t Skipped = 0;
        uint64_t Replaced = 0;
        uint64_t Stopped = 0;
        uint64_t Failed = 0;
        uint64_t StalledFrames = 0;     // frames spent waiting on a chunk, all killcams
        uint64_t DeathViews = 0;        // killcams whose view reached the kill (one "death view" line each)
        // ME-343: replay worlds built (Prepare, or a killcam with none kept), killcams played in a kept one, and
        // kept ones thrown away because they couldn't be made whole again (or didn't fit a stream).
        uint64_t WorldsBuilt = 0;
        uint64_t WorldsReused = 0;
        uint64_t WorldsDiscarded = 0;
        // Killcams let go by Skip before they played (ME-343), not in Skipped.
        uint64_t SkippedUnplayed = 0;
    };
    const Stats& GetStats() const { return m_stats; }
    // The newest killcam id looked at this session (0: none; it starts over when the session does).
    uint32_t GetLastSeenId() const { return m_lastSeen; }

private:
    void Start( World& InLive, const NetKillcamReceiver& InKillcam );
    // InKeep: reset the world for the next killcam (KeepWorld), else destroy it.
    void End( const std::string& InReason, bool InKeep = true );
    // A replay world for InLive (m_replay, m_built): false with OutError when Populate failed.
    bool Build( World& InLive, std::string& OutError );
    // Back to how it was built: the playback stopped, what's not of the build destroyed, the killer's body
    // shown. False when it can't be (a built entity is gone).
    bool ResetWorld();
    void DestroyWorld();
    bool StartPlayback( const NetKillcamReceiver& InKillcam, std::string& OutError );
    void PlaceView();
    // m_samples from the tick before InTime as far as the killer's view of InTime needs (to the death tick,
    // and what the stream has so far).
    void ReadSamples( double InTime );
    // While chunks are still coming: the world tick the killer's last tick in was looking at (the playback
    // holds there, so the killer's view of what it shows is in); < 0: no hold (a bot's, or the death is in).
    double EdgeViewTick();
    // The killer's shot at the death tick nearest the view's aim (Status::HasShot ...).
    void FindKillShot();
    void Fire( bool InStarted, const std::string& InReason );
    // The hold on the last tick: HoldSeconds when it's the death's, HoldAfterSeconds past it.
    double HoldSeconds() const { return m_status.LastTick > m_status.DeathTick ? m_settings.HoldAfterSeconds : m_settings.HoldSeconds; }
    // Stops the download of killcam InId if it's InLive's newest, not yet whole (ME-343).
    void CancelDownload( World* InLive, uint32_t InId );
    void Bind( World& InLive );

    Settings m_settings;
    Populate m_populate;
    // The replay world, kept between killcams (ME-343), and whether a killcam plays in it.
    std::unique_ptr<ReplayWorld> m_replay;
    bool m_playing = false;
    WeakPtr<World> m_builtFor;          // the live world it was built for
    std::vector<uint64_t> m_built;      // its entities as built (EntityID values, sorted)
    std::vector<std::pair<uint64_t, uint8_t>> m_hidden;    // the killer's parts hidden: entity, 1 Mesh 2 Model 4 TextRenderer
    bool m_prepared = false;            // Prepare has built (or tried): it doesn't again until Release
    // A world built afresh refused a stream too (ME-343): a refusal is the streams' (an additive scene, a
    // recording of another), so the kept world's no longer builds another, until Release.
    bool m_freshRefused = false;
    WeakPtr<World> m_live;
    uint32_t m_lastSeen = 0;        // the newest killcam id looked at (started, or failed), this session
    Status m_status;
    Stats m_stats;
    double m_held = 0.0;            // seconds on the death tick, complete
    double m_stalled = 0.0;         // seconds waiting on a chunk, now
    bool m_deathShown = false;      // the kill's view was logged
    // The killer's ticks ahead of the playhead (ME-343), read from the stream with a reader of their own.
    std::unique_ptr<NetKillcamSource> m_aheadSource;
    std::unique_ptr<NetReplayReader> m_ahead;
    std::vector<KillerSample> m_samples;
    uint32_t m_edgeTick = 0;        // the last tick in when m_edgeView was read
    double m_edgeView = -1.0;
    bool m_skipRequested = false;
};
