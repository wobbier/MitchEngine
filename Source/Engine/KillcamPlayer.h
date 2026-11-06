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
// killer's last seconds, streaming in). Once its first chunk is in, the player makes a second World
// with its own NetCore (and, through Populate, the scene and whatever cores present it: the engine's
// camera, render and script cores) and plays the stream into it through ME-316's playback path
// (NetCore::StartPlayback over a NetKillcamSource): nothing is predicted, every entity follows the
// recording, interpolated, and the playback waits on the last tick there is while chunks are still
// coming (NetReplayPlayback::SetStreaming). The live world isn't touched: it keeps receiving,
// predicting and simulating, and its session never waits on the killcam.
//
// Each frame Update moves the playhead, poses the replay world, and puts its main camera at the
// killer's eye looking along the killer's recorded aim (its client's commands, interpolated between
// ticks; a bot's facing), with the killer's own meshes hidden (first person). It ends once the death
// tick has shown for HoldSeconds, on Skip, when a newer killcam replaces it, when the session ends
// (Stop), or when the stream stalls; then the replay world goes, at once.
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
        // Seconds the death tick stays on screen before the switch back.
        double HoldSeconds = 1.0;
        // Seconds the playback may wait on a chunk that doesn't come before it gives up.
        double MaxStallSeconds = 3.0;
        // The replay world's main camera at the killer's eye along its recorded aim.
        bool KillerView = true;
        // The eye above the killer's origin; < 0: its CharacterController's top less 0.15 m (1.6 m without one).
        float EyeHeight = -1.f;
        // The killer's own meshes, models and text hidden in the killer's view.
        bool HideKiller = true;
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

    KillcamPlayer() = default;
    ~KillcamPlayer();
    KillcamPlayer( const KillcamPlayer& ) = delete;
    KillcamPlayer& operator=( const KillcamPlayer& ) = delete;

    void SetPopulate( Populate InPopulate ) { m_populate = std::move( InPopulate ); }
    Settings& GetSettings() { return m_settings; }

    // Every frame, once the live world's frame is simulated (Engine::Run: after its late update, before
    // presentation): starts a killcam that has arrived, moves the one playing by InFrameSeconds, places
    // the camera, and ends it when it's done. InLive: the world whose NetCore receives killcams.
    void Update( World& InLive, double InFrameSeconds );
    // Ends the killcam playing (C#'s Net.Killcam.Skip) at the start of the next Update, which in the
    // engine is later the same frame for a live world's script: the replay world goes then (never while
    // its own scripts are running, which may be what asked).
    void Skip() { m_skipRequested = m_replay != nullptr; }
    // Ends whatever plays ("stopped"): a scene load, the session's end, shutdown.
    void Stop();

    bool IsPlaying() const { return m_replay != nullptr; }
    // The replay world while one plays (null otherwise).
    World* GetWorld() const;
    SharedPtr<World> GetWorldPtr() const;
    NetCore* GetNet() const { return m_replay ? m_replay->Net : nullptr; }

    // The killcam playing, or the last one played.
    struct Status
    {
        uint32_t Id = 0;
        uint32_t KillerNetId = 0;
        uint32_t VictimNetId = 0;
        uint8_t KillerClient = 0;   // 0: a bot's
        uint32_t FirstTick = 0;
        uint32_t DeathTick = 0;
        uint16_t TickRate = 60;
        double Tick = 0.0;          // the tick shown (fractional between ticks)
        double Elapsed = 0.0;       // seconds since it started
        double Duration = 0.0;      // seconds it plays: first tick to the death at its tick rate, then the hold
        bool Complete = false;      // every chunk is in
        // The replay world's main camera was put at the killer's eye this frame (KillerView, and the world
        // has a camera entity to move), for logs and captures. Eye / Yaw / Pitch: the killer's view,
        // placed or not.
        bool HasView = false;
        Vector3 Eye;
        float Yaw = 0.f;
        float Pitch = 0.f;
    };
    const Status& GetStatus() const { return m_status; }

    struct Stats
    {
        uint64_t Started = 0;
        uint64_t Played = 0;
        uint64_t Skipped = 0;
        uint64_t Replaced = 0;
        uint64_t Stopped = 0;
        uint64_t Failed = 0;
        uint64_t StalledFrames = 0;     // frames spent waiting on a chunk, all killcams
    };
    const Stats& GetStats() const { return m_stats; }
    // The newest killcam id looked at this session (0: none; it starts over when the session does).
    uint32_t GetLastSeenId() const { return m_lastSeen; }

private:
    void Start( World& InLive, const NetKillcamReceiver& InKillcam );
    void End( const std::string& InReason );
    void PlaceView();
    void Fire( bool InStarted, const std::string& InReason );
    void Bind( World& InLive );

    Settings m_settings;
    Populate m_populate;
    std::unique_ptr<ReplayWorld> m_replay;
    WeakPtr<World> m_live;
    uint32_t m_lastSeen = 0;        // the newest killcam id looked at (started, or failed), this session
    Status m_status;
    Stats m_stats;
    double m_held = 0.0;            // seconds on the death tick, complete
    double m_stalled = 0.0;         // seconds waiting on a chunk, now
    bool m_skipRequested = false;
};
