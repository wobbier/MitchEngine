#pragma once
#include "ECS/Core.h"
#include "Events/Event.h"
#include "Net/NetKillcam.h"
#include "Net/NetLagCompensation.h"
#include "Net/NetPrediction.h"
#include "Net/NetReplay.h"
#include "Net/NetReplayPlayback.h"
#include "Net/NetReplayResim.h"
#include "Net/NetReplication.h"
#include "Net/NetSession.h"
#include <deque>
#include <future>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class World;

// Who came and went in a network session, for gameplay: every listener hears every event
// (Listen<NetSessionEvent>, checking Source against its world). ScriptCore hands its world's to
// C#'s EventBus: NetClientJoined / NetClientLeft on the server, NetConnected / NetDisconnected on a
// client. Fired on the main thread by the NetCore whose session it is, once its Receive or Close
// has finished (never from inside the session's own loops); Engine::Run's Receive comes before
// the frame's fixed steps and scripts.
// - The server: a client was accepted (its Welcome is on the way), or left ("left", "timed out",
//   or "closed" by Close).
// - A client: it was welcomed, or its session ended ("left" by Close, what the server said when
//   it closed, "timed out", "rejected: <why>"). The server's words are valid UTF-8 whatever it
//   sent.
// Engine::Run closes the session after its loop ends: C++ listeners still alive hear "closed" and
// a client's own "left", scripts never do.
class NetSessionEvent
    : public Event<NetSessionEvent>
{
public:
    enum class Kind : uint8_t
    {
        ClientJoined,
        ClientLeft,
        Connected,
        Disconnected,
    };
    Kind Type = Kind::ClientJoined;
    uint8_t ClientId = 0;   // a client's own id with Connected; 0 with Disconnected
    std::string Reason;     // ClientLeft and Disconnected
    // The world whose NetCore it is (NET-0: several worlds can hold sessions in one process).
    const World* Source = nullptr;
};

// Networking's engine-owned core (NET-2). It runs nothing until Host or Connect starts a session
// over a transport; then the engine's frame drives it:
//
// - Receive( now ) after the frame's events: packets, the handshake, commands, server reports,
//   then the NetSessionEvents they caused.
// - BeginTick / EndTick around each fixed step: the server takes each client's command for the
//   tick, then reports; the client stamps the local command with its tick.
// - WriteInterpolated( frame seconds ) after the frame's physics poses: the client poses what the
//   server replicates (NET-3), interpolated.
// - Flush( now ) before the frame limiter: the client's one packet of unconfirmed commands.
// - GetClockScale: the client's time dilation; the engine scales its fixed-step accumulator by it,
//   so ticks run up to 5% faster or slower without changing their length.
//
// NetCore drives the transport's Update (Receive, EndTick, Flush), so conditioned links release
// what's due. Close says goodbye before Shutdown; WriteReport sums the session up (--net-report).
//
// Scripts (NET-6): ScriptGlue (installed by the engine when C# runs; tests install their own)
// carries C# [Replicated] / [Predicted] state with entities, re-runs the predicted pawn's scripts
// in resimulation, and delivers RPCs. Ownership: SetOwner on the server reaches every client; a
// client that gets a character predicts it. Scripts on networked entities simulate (fixed steps)
// on the server and, on a client, only on its predicted pawn (SimulatesLocally).
//
// It never calls GetEngine(): tests run a server world and a client world side by side
// (Tests/Source/NetHarness.h).
class NetCore final
    : public Core<NetCore>
{
public:
    NetCore();
    ~NetCore() override;

    // A session over InTransport, which must outlive it. False when one is already running.
    bool Host( INetTransport& InTransport, uint16_t InTickRate = 60 );
    bool Connect( INetTransport& InTransport, NetAddress InServer, const NetClockSettings& InClock = NetClockSettings() );
    // Says Disconnect to the other side (the server tells every client), so nobody waits out a
    // timeout. The session stays readable (stats, WriteReport) until Shutdown.
    void Close( double InNow );
    void Shutdown();
    // Seconds of silence before a peer counts as gone; applies to sessions started afterwards.
    void SetTimeout( double InSeconds ) { m_timeout = InSeconds; }

    struct ScriptGlue
    {
        NetScriptState State;   // [Replicated] bytes in snapshots, [Predicted] bytes for the owner
        std::function<void( Entity&, const std::vector<uint8_t>& )> RestorePredicted;
        std::function<void( Entity& )> SimulatePredicted;   // one fixed step of the entity's scripts
        // An RPC arrived for an entity (the server checked the sender owns it).
        std::function<void( Entity&, int InRpcId, const uint8_t* InData, size_t InSize )> DispatchRpc;
        // The networked script classes' schema, folded into the handshake.
        std::function<uint64_t()> SchemaHash;
    };
    // Before Host / Connect.
    void SetScriptGlue( ScriptGlue InGlue ) { m_glue = std::move( InGlue ); }
    // The networked input actions' layout (names, order, kinds), folded into the handshake.
    void SetCommandSchema( uint64_t InHash ) { m_commandSchema = InHash; }
    uint64_t GetCommandSchema() const { return m_commandSchema; }

    // The command an entity runs with this tick, and the one before (button edges): on the server
    // its owner's; on a client its predicted pawn's (a resimulated tick's while resimulating).
    bool GetCommand( Entity& InEntity, InputCommand& OutCommand, InputCommand& OutPrevious ) const;
    // (A replay's resim check counts too: ResimulateReplay.)
    bool IsResimulating() const { return ( m_predictor && m_predictor->IsResimulating() ) || m_replayResim.Active; }
    // Whether this side runs an entity's fixed-step scripts: anything not networked, everything on
    // the server, and only the predicted pawn on a client (the rest follows the server).
    bool SimulatesLocally( Entity& InEntity ) const;
    // Ownership: 0 is the server. SetOwner only means something on the server.
    uint8_t GetOwner( Entity& InEntity ) const;
    void SetOwner( Entity& InEntity, uint8_t InClient );
    bool IsPredicted( Entity& InEntity ) const;
    uint32_t GetNetId( Entity& InEntity ) const;
    // The entity's NetId, or its nearest networked ancestor's (a pawn's Head hitbox); 0 for none.
    static uint32_t GetNetIdOrAncestor( const EntityHandle& InEntity );
    EntityHandle FindEntity( uint32_t InNetId ) const;
    // RPC kinds: to the server (from the owner), to the entity's owner, to every client. False
    // when this side can't send that kind or the message is too big (1 KB).
    enum class RpcKind : int
    {
        ToServer = 0,
        ToOwner = 1,
        ToAll = 2,
    };
    bool SendRpc( Entity& InEntity, int InRpcId, RpcKind InKind, const uint8_t* InData, size_t InSize );

    // The session so far as JSON (role, connection, timeouts, snapshot rates): true when written.
    bool WriteReport( const std::string& InPath, double InNow ) const;

    bool IsActive() const { return m_server || m_client || m_playback; }
    // A session is under way (ME-311): hosting until Close, or a client connecting or connected.
    // A client whose session ended (left, rejected, timed out, disconnected by the server) isn't,
    // though it stays IsActive (readable) until Shutdown; nor is anything after Close.
    bool IsSessionLive() const;
    bool IsServer() const { return m_server != nullptr; }
    // A client of a server, or a replay viewer (ME-316): the world follows replicated state, and
    // networked scripts present it rather than simulate it.
    bool IsClient() const { return m_client != nullptr || m_playback != nullptr; }
    NetServer* GetServer() { return m_server.get(); }
    NetClient* GetClient() { return m_client.get(); }
    NetReplicationServer* GetReplicationServer() { return m_replicationServer.get(); }
    NetReplicationClient* GetReplicationClient() { return m_replicationClient.get(); }

    // What this world presents in the handshake: the project's product name and version, the C#
    // script classes, and the networked (NetIdentity) entities placed when the session starts.
    NetVersion MakeVersion();
    // Pins the version instead (tests; a mismatched build).
    void SetVersionOverride( const NetVersion& InVersion ) { m_versionOverride = InVersion; }

    // Frame hooks. BeginTick's InTicksOwed: the fixed steps still due after this one (a server
    // catching up after a hitch runs them back to back); a server measures its clients' slack
    // against where it will be once caught up, so its lag doesn't read as their clocks being ahead.
    void Receive( double InNow );
    void BeginTick( uint32_t InTicksOwed = 0 );
    void EndTick( double InNow );
    void Flush( double InNow );
    // After the frame's physics poses: the client shows mirrored entities interpolated (NET-3).
    void WriteInterpolated( double InFrameSeconds );
    double GetClockScale() const;
    // The predicted pawn's visual offset (NET-7), for presentation only. BeginPresentation adds it to
    // the pawn's Transform once gameplay is done with the frame (after the late update), so its
    // children (meshes, an attached camera, audio sources), audio, cameras and render prep draw it
    // where it was before a correction, gliding to where it is; EndPresentation puts the simulated
    // pose back, exactly, once the frame has rendered. Gameplay, physics and the network never see
    // the offset (Receive and BeginTick end a presentation left open). A Transform moved in between
    // (the editor) keeps the move.
    void BeginPresentation();
    void EndPresentation();
    bool IsPresenting() const { return m_presentation.Active; }
    // A server never drops ticks to catch up: every client's commands need simulating.
    bool KeepsBacklog() const { return IsServer(); }

    // The local player's input for the coming ticks (until changed).
    void SetLocalCommand( const InputCommand& InCommand ) { m_localCommand = InCommand; }

    // How a command drives an entity (NET-4): on the server for every entity a client owns
    // (NetIdentity::SetOwner) with that client's command; on a client for its predicted pawn.
    void SetCommandHandler( NetPredictor::CommandHandler InHandler ) { m_commandHandler = std::move( InHandler ); }
    // Client: predict this pawn (a CharacterController the server lets this client own).
    void Predict( EntityHandle InPawn );
    NetPredictor* GetPredictor() { return m_predictor.get(); }

    // Server (NET-8): a ray against the hitboxes as InCommand's player saw them (its ViewTick),
    // blocked by colliders in the present. InShooter and everything under it (a pawn's head
    // hitbox) are ignored. InInstigator: who fired it when InShooter is a projectile sweeping its
    // flight (the replay attributes the shot to them; none: the shooter fired it).
    LagCompensatedHit LagCompensatedRaycast( const Vector3& InOrigin, const Vector3& InDirection, float InMaxDistance, const InputCommand& InCommand,
        const EntityHandle& InShooter = EntityHandle(), const EntityHandle& InInstigator = EntityHandle() );
    // The same ray against the hitboxes where they are now (a client's own view, for impact FX it
    // predicts; offline play), with no rewind: what NET-15's Net.LagCompensatedRaycast does off
    // the server. InShooter and everything under it are ignored.
    LagCompensatedHit PresentRaycast( const Vector3& InOrigin, const Vector3& InDirection, float InMaxDistance, const EntityHandle& InShooter = EntityHandle() );
    NetLagCompensation& GetLagCompensation() { return m_lagCompensation; }
    // Smoothed round trip in ms: a client's to its server, or the server's to one client (0
    // without a session or before the first ack).
    float GetRoundTripMs( uint8_t InClient = 0 ) const;
    // The tick being simulated (BeginTick to EndTick): the server's, or the client's.
    // (During a resimulation: the tick being replayed.)
    uint32_t GetTick() const
    {
        return m_replayResim.Active ? m_replayResim.Tick : m_predictor && m_predictor->IsResimulating() ? m_predictor->GetCommand().Tick : m_tick;
    }

    // Match replays (ME-316, NetReplay.h): a server records every tick after its CaptureTick: the
    // whole capture, each character's mover and [Predicted] state, each client's command and link,
    // owed and dropped ticks, entity names and owners, every LagCompensatedRaycast (with the
    // rewound hitboxes and its instigator), joins and leaves, and
    // RecordReplayEvent's. The chunks are written on the background lane. InPath "": the ring
    // only. The header's tick rate and hashes come from the session. False on a client, while
    // recording, or when the file can't be written.
    bool StartRecording( const std::string& InPath, NetReplayHeader InHeader = NetReplayHeader(), const NetReplaySettings& InSettings = NetReplaySettings() );
    // Writes the last chunk and the index and waits (logged); the recorder stays readable until
    // Shutdown or the next StartRecording. Shutdown stops it too.
    void StopRecording();
    bool IsRecording() const { return m_recorder && !m_recorder->IsClosed(); }
    const NetReplayRecorder* GetRecorder() const { return m_recorder.get(); }
    // An event at the current tick into the recording (nothing without one): C#'s Replay.Event.
    // The name is cut to 64 bytes, the payload to 4000.
    void RecordReplayEvent( const std::string& InName, const std::string& InPayload, uint32_t InNetId = 0 );
    // ... about an entity: its NetId (or its nearest networked ancestor's) is looked up when the tick
    // is recorded, after CaptureTick, so an entity spawned this tick has its id already. One destroyed
    // before then keeps the id it had when the event was raised.
    void RecordReplayEvent( const std::string& InName, const std::string& InPayload, const EntityHandle& InEntity );
    // Engine::Run: backlog ticks it dropped (they never run); the next recorded tick says so.
    void NoteDroppedTicks( uint32_t InTicks ) { m_ticksDropped += InTicks; }

    // Killcams (ME-323 phase 2, NetKillcam.h). Server: the last InSeconds up to this tick (the
    // death) from the replay ring (StartRecording, a file or the ring alone: nothing is captured
    // twice), filtered to what the killer's client would have been sent (NET-13 relevancy from the
    // killer's pawn, plus the victim), with the killer's commands and aim, mover, shots and events,
    // in ME-316 chunks streamed to the victim's owner over the blob channel. It's cut once the tick
    // is recorded (EndTick), encoded on the background lane, then sent a chunk a blob: the first
    // alone as soon as it's encoded (the whole share is its, so the victim can start on it), the
    // rest at once when it's acked. A newer killcam for the same client cancels the older.
    // False without a ring, on a client, for an unknown or unaccepted client, a bot victim (owner
    // 0), or a killer that isn't networked. InSeconds is capped to the ring.
    struct KillcamSettings
    {
        uint32_t ChunkTicks = 60;       // a chunk (a blob) every this many ticks
    };
    void SetKillcamSettings( const KillcamSettings& InSettings ) { m_killcamSettings = InSettings; }
    bool SendKillcam( const EntityHandle& InVictim, const EntityHandle& InKiller, double InSeconds = 8.0 );
    bool SendKillcam( uint8_t InClient, uint32_t InVictimNetId, uint32_t InKillerNetId, double InSeconds = 8.0 );
    struct KillcamStats
    {
        uint64_t Requested = 0;
        uint64_t Encoded = 0;       // cut and queued to send
        uint64_t Completed = 0;     // every blob acked
        uint64_t Failed = 0;        // nothing to send (the range isn't in the ring), a send refused
        uint64_t Cancelled = 0;     // replaced by a newer one for the client, or the client left
        uint64_t Bytes = 0;         // encoded
        uint64_t Ticks = 0;
        double EncodeMs = 0.0;      // on the lane, all of them
    };
    const KillcamStats& GetKillcamStats() const { return m_killcamStats; }
    // Killcams waiting for their tick, encoding or streaming (server).
    size_t GetKillcamsPending() const { return m_killcams.size(); }
    // Client: the newest killcam the server sent (its chunks as they arrive, in a replay source).
    const NetKillcamReceiver& GetKillcam() const { return m_killcam; }
    // 0..1: the newest killcam's bytes in (blobs whole and on their way); 1 once complete, 0 before any.
    float GetKillcamProgress() const;

    // Match replay playback (ME-316 phase 2, NetReplayPlayback.h): plays InSource into this world
    // through the client's replication path, with no session: every networked entity follows the
    // recording (nothing is predicted), networked scripts present rather than simulate (IsClient),
    // and the frame's WriteInterpolated moves the playhead (paused at the first tick to begin with).
    // GetTick is the tick shown. False when a session or playback is running, the replay is empty,
    // or it was recorded on another scene (its placed NetIds wouldn't match; OutError says why).
    // A different NetSchema or script schema only warns: fields may not decode.
    bool StartPlayback( std::unique_ptr<INetReplaySource> InSource, std::string* OutError = nullptr );
    bool IsPlayback() const { return m_playback != nullptr; }
    NetReplayPlayback* GetPlayback() { return m_playback.get(); }
    const NetReplayPlayback* GetPlayback() const { return m_playback.get(); }
    // The world against the recording at the tick shown (NetReplayPlayback::CheckWorld, with the
    // script glue's [Replicated] bytes).
    NetReplayPlayback::Check CheckPlayback() const;
    // Resim check (ME-316 phase 3, NetReplayResim.h) while a replay plays: InNetId's character in this
    // world simulated again from its recorded mover (and [Predicted] script state) over InFrom..InTo
    // with its owner's recorded commands, the way the owner's prediction resimulates: the command
    // handler, the scripts' fixed step (Net.IsResimulating is true, Net.Command and Net.Tick are the
    // recorded tick's, the entity counts as predicted), then PhysicsCore::StepCharacter; each tick is
    // compared with the recorded mover. The rest of the world stays at the tick shown (other
    // characters' capsules included). The character's mover and [Predicted] state are put back
    // afterwards. Its Error says why it couldn't run (no playback, no such character, no physics).
    NetReplayResim::Result ResimulateReplay( uint32_t InNetId, uint32_t InFrom, uint32_t InTo, const NetReplayResim::Settings& InSettings = NetReplayResim::Settings() );

#if USING( ME_EDITOR )
    void OnEditorInspect() final;
#endif

private:
    void RecordReplayTick();
    void RecordShot( const Vector3& InOrigin, const Vector3& InDirection, float InMaxDistance, double InViewTick, const EntityHandle& InShooter,
        const EntityHandle& InInstigator, const LagCompensatedHit& InHit );

    // A client the server no longer has: what the report says about it.
    struct Departure
    {
        uint8_t Id = 0;
        NetAddress Address = 0;
        std::string Reason;
        double ConnectedSeconds = 0.0;
        NetStats Stats;
        uint64_t Snapshots = 0;
        uint64_t SnapshotBytes = 0;
        uint64_t MissedCommands = 0;
        uint64_t LateCommands = 0;
    };
    void RecordDeparture( const NetServer::Client& InClient, double InNow );
    // Session events wait here until the public call that raised them (Receive, Close) is done,
    // then FireSessionEvents sends them as NetSessionEvents.
    void PushSessionEvent( NetSessionEvent::Kind InType, uint8_t InClientId, std::string InReason = {} );
    void FireSessionEvents();
    // A client: the state change since it last looked, as a session event.
    void NoteClientState();
    void OnRpc( uint8_t InFromClient, BitReader& InReader );
    void OnOwnerChanged( uint32_t InNetId, uint8_t InOwner );

    std::unique_ptr<NetServer> m_server;
    std::unique_ptr<NetClient> m_client;
    std::unique_ptr<NetReplicationServer> m_replicationServer;
    std::unique_ptr<NetReplicationClient> m_replicationClient;
    std::optional<NetVersion> m_versionOverride;
    InputCommand m_localCommand;
    NetPredictor::CommandHandler m_commandHandler;
    std::unique_ptr<NetPredictor> m_predictor;
    NetLagCompensation m_lagCompensation;
    // BeginPresentation's pawn, its simulated local position and the one presented.
    struct Presentation
    {
        bool Active = false;
        EntityHandle Pawn;
        Vector3 Simulated;
        Vector3 Presented;
    } m_presentation;
    uint32_t m_tick = 0;
    INetTransport* m_transport = nullptr;
    ScriptGlue m_glue;
    uint64_t m_commandSchema = 0;
    double m_timeout = NetServer::kDefaultTimeout;
    double m_startedAt = -1.0;
    double m_lastNow = 0.0;
    bool m_closing = false;
    bool m_closed = false;   // Close ran (until the next Host / Connect / Shutdown)
    std::vector<Departure> m_departures;
    std::vector<NetSessionEvent> m_sessionEvents;
    bool m_firingSessionEvents = false;
    NetClient::State m_reportedClientState = NetClient::State::Disconnected;
    // When snapshots first and most recently arrived (client), for the snapshot rate.
    double m_firstSnapshotAt = -1.0;
    double m_lastSnapshotAt = -1.0;
    uint64_t m_firstSnapshotCount = 0;
    uint64_t m_lastSnapshotCount = 0;
    // ME-316: the recording, and what the next recorded tick carries.
    std::unique_ptr<NetReplayRecorder> m_recorder;
    std::vector<NetReplayShot> m_replayShots;
    // An event and the entity it's about (its NetId is resolved after CaptureTick).
    struct PendingReplayEvent
    {
        NetReplayEvent Event;
        EntityHandle Entity;
    };
    std::vector<PendingReplayEvent> m_replayEvents;
    std::map<uint32_t, uint8_t> m_replayOwners;     // NetId -> the owner last recorded
    std::map<uint8_t, uint64_t> m_replayMisses;     // client -> its misses at the last recorded tick
    std::map<uint32_t, uint32_t> m_replayAlwaysRelevant;    // NetId -> the last recorded tick it was AlwaysRelevant at (pruned with the ring)
    uint32_t m_ticksOwed = 0;
    uint32_t m_ticksDropped = 0;
    // ME-316 playback: the source, then what reads it (destroyed first).
    std::unique_ptr<INetReplaySource> m_replaySource;
    std::unique_ptr<NetReplayPlayback> m_playback;
    std::string m_playbackError;    // logged once
    // ResimulateReplay's tick being simulated: the entity it's for, its command and the one before.
    struct ReplayResimTick
    {
        bool Active = false;
        uint64_t Entity = 0;    // EntityID value
        uint32_t Tick = 0;
        InputCommand Command;
        InputCommand Previous;
    };
    ReplayResimTick m_replayResim;
    // ME-323 killcams. Server: each one from its request to its last blob acked.
    struct KillcamSend
    {
        NetKillcamRequest Request;
        uint8_t Client = 0;
        bool Encoding = false;
        std::future<NetKillcam::Encoded> Future;
        std::deque<std::vector<uint8_t>> Queued;
        std::vector<uint32_t> InFlight;     // blob ids
        bool Streaming = false;
        uint32_t FirstId = 0;               // the first chunk's blob: it goes alone, until it's acked
    };
    void UpdateKillcams();
    void CancelKillcam( KillcamSend& InSend );
    void OnBlob( std::vector<uint8_t>& InData );
    KillcamSettings m_killcamSettings;
    std::vector<KillcamSend> m_killcams;
    uint32_t m_nextKillcamId = 1;
    KillcamStats m_killcamStats;
    // Client.
    NetKillcamReceiver m_killcam;
};

ME_REGISTER_CORE( NetCore )
