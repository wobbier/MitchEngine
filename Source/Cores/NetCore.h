#pragma once
#include "ECS/Core.h"
#include "Net/NetLagCompensation.h"
#include "Net/NetPrediction.h"
#include "Net/NetReplication.h"
#include "Net/NetSession.h"
#include <memory>
#include <optional>

// Networking's engine-owned core (NET-2). It runs nothing until Host or Connect starts a session
// over a transport; then the engine's frame drives it:
//
// - Receive( now ) after the frame's events: packets, the handshake, commands, server reports.
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

    // The command an entity runs with this tick, and the one before (button edges): on the server
    // its owner's; on a client its predicted pawn's (a resimulated tick's while resimulating).
    bool GetCommand( Entity& InEntity, InputCommand& OutCommand, InputCommand& OutPrevious ) const;
    bool IsResimulating() const { return m_predictor && m_predictor->IsResimulating(); }
    // Whether this side runs an entity's fixed-step scripts: anything not networked, everything on
    // the server, and only the predicted pawn on a client (the rest follows the server).
    bool SimulatesLocally( Entity& InEntity ) const;
    // Ownership: 0 is the server. SetOwner only means something on the server.
    uint8_t GetOwner( Entity& InEntity ) const;
    void SetOwner( Entity& InEntity, uint8_t InClient );
    bool IsPredicted( Entity& InEntity ) const;
    uint32_t GetNetId( Entity& InEntity ) const;
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

    bool IsActive() const { return m_server || m_client; }
    bool IsServer() const { return m_server != nullptr; }
    bool IsClient() const { return m_client != nullptr; }
    NetServer* GetServer() { return m_server.get(); }
    NetClient* GetClient() { return m_client.get(); }
    NetReplicationServer* GetReplicationServer() { return m_replicationServer.get(); }
    NetReplicationClient* GetReplicationClient() { return m_replicationClient.get(); }

    // What this world presents in the handshake: the project's product name and version, the C#
    // script classes, and the networked (NetIdentity) entities placed when the session starts.
    NetVersion MakeVersion();
    // Pins the version instead (tests; a mismatched build).
    void SetVersionOverride( const NetVersion& InVersion ) { m_versionOverride = InVersion; }

    // Frame hooks.
    void Receive( double InNow );
    void BeginTick();
    void EndTick( double InNow );
    void Flush( double InNow );
    // After the frame's physics poses: the client shows mirrored entities interpolated (NET-3).
    void WriteInterpolated( double InFrameSeconds );
    double GetClockScale() const;
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
    // blocked by colliders in the present. InShooter is ignored.
    LagCompensatedHit LagCompensatedRaycast( const Vector3& InOrigin, const Vector3& InDirection, float InMaxDistance, const InputCommand& InCommand,
        const EntityHandle& InShooter = EntityHandle() );
    // The same ray against the hitboxes where they are now (a client's own view, for impact FX it
    // predicts; offline play), with no rewind: what NET-15's Net.LagCompensatedRaycast does off
    // the server. InShooter is ignored.
    LagCompensatedHit PresentRaycast( const Vector3& InOrigin, const Vector3& InDirection, float InMaxDistance, const EntityHandle& InShooter = EntityHandle() );
    NetLagCompensation& GetLagCompensation() { return m_lagCompensation; }
    // Smoothed round trip in ms: a client's to its server, or the server's to one client (0
    // without a session or before the first ack).
    float GetRoundTripMs( uint8_t InClient = 0 ) const;
    // The tick being simulated (BeginTick to EndTick): the server's, or the client's.
    // (During a resimulation: the tick being replayed.)
    uint32_t GetTick() const { return m_predictor && m_predictor->IsResimulating() ? m_predictor->GetCommand().Tick : m_tick; }

#if USING( ME_EDITOR )
    void OnEditorInspect() final;
#endif

private:
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
    uint32_t m_tick = 0;
    INetTransport* m_transport = nullptr;
    ScriptGlue m_glue;
    uint64_t m_commandSchema = 0;
    double m_timeout = NetServer::kDefaultTimeout;
    double m_startedAt = -1.0;
    double m_lastNow = 0.0;
    bool m_closing = false;
    std::vector<Departure> m_departures;
    // When snapshots first and most recently arrived (client), for the snapshot rate.
    double m_firstSnapshotAt = -1.0;
    double m_lastSnapshotAt = -1.0;
    uint64_t m_firstSnapshotCount = 0;
    uint64_t m_lastSnapshotCount = 0;
};

ME_REGISTER_CORE( NetCore )
