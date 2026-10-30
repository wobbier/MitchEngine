#pragma once
#include "Components/Physics/CharacterController.h"
#include "ECS/EntityHandle.h"
#include "Net/NetSession.h"
#include "Net/NetSnapshot.h"
#include <deque>
#include <functional>
#include <map>
#include <set>
#include <unordered_map>
#include <vector>

class World;

// Replication (NET-3): the server's networked entities (NetIdentity) mirrored on every client.
//
// - Ids: placed entities get static NetIds on both sides (sorted GUIDs, 1..N); entities the
//   server creates later get dynamic ones (from kFirstDynamicId) and a reliable Spawn
//   {NetId, owner, prefab, pose} to every client, sent again to late joiners. Destroying one
//   sends Despawn.
// - Relevancy and bandwidth (NET-13, NetReplicationSettings): each client only gets what's
//   within RelevantRadius of its viewer; changed entities build up priority (NetIdentity::
//   Priority, falling off with distance) every tick they wait, and the snapshot takes the highest
//   first until the client's byte budget runs out, so near things update often and nothing
//   starves. Every client's snapshot is encoded on the job system.
// - Snapshots: every tick each client gets the world's state as a delta against the newest
//   snapshot it acked (or everything), quantized (NetSnapshot). When nothing changed since the
//   last one sent, the server skips it, sending one at least every kKeepAliveTicks, so an idle
//   scene costs next to nothing.
// - Owners: each packet also carries, for every character the client owns, its exact mover
//   state after the tick (OwnerState), which the client's NetPredictor reconciles with (NET-4).
// - Clients keep the decoded snapshots (deltas need them), interpolate each entity at
//   "server tick - delay" (adaptive: it grows when samples run out) and make remote Rigidbodies
//   kinematic: only the server simulates.
// - Scripts (NET-6, NetScriptState): an entity's C# [Replicated] fields ride in its snapshot
//   fields after the reflected ones; its owner also gets the [Predicted] ones in OwnerState.
//   Ownership changes go out as Owner messages (late joiners get the current owners), and
//   messages replication doesn't know (RPCs) go to OnOtherMessage.
// C# script state riding with entities (NET-6); NetCore fills it in when scripting runs.
struct NetScriptState
{
    // Appends the entity's [Replicated] (and [Predicted]) script bytes, if it has any.
    std::function<void( Entity&, std::vector<uint8_t>& )> WriteReplicated;
    // InOwned: the entity is this client's predicted pawn ([Predicted] fields come from OwnerState).
    std::function<void( Entity&, const std::vector<uint8_t>&, bool InOwned )> ReadReplicated;
    // Appends the entity's [Predicted] script bytes, if it has any.
    std::function<void( Entity&, std::vector<uint8_t>& )> SavePredicted;
};

// How the server shares out each client's bandwidth (NET-13).
struct NetReplicationSettings
{
    // Entities further than this from a client's viewer (SetViewer, else the first entity it
    // owns) aren't sent to it; they hold their last state there. 0: everything is relevant.
    float RelevantRadius = 0.f;
    // Priority falls off past this: an entity at 4x the distance accumulates a quarter as fast.
    float NearDistance = 10.f;
    // Snapshot bytes a second per client (a token bucket, up to two ticks' worth saved up).
    // 0: only the per-packet budget limits it.
    uint32_t BytesPerSecond = 0;
    // Encode each client's snapshot on the job system.
    bool UseJobs = true;
};


class NetReplicationServer
{
public:
    NetReplicationSettings Settings;
    static constexpr uint32_t kFirstDynamicId = 0x10000;
    static constexpr uint32_t kKeepAliveTicks = 15;
    static constexpr size_t kSnapshotBudget = 900;

    NetScriptState Scripts;

    NetReplicationServer( World& InWorld, NetServer& InServer );

    // Before the first tick: static ids for what's placed.
    void AssignStaticIds();
    // After a simulated tick: ids for new entities, spawns, despawns and this tick's state.
    void CaptureTick( uint32_t InTick );

    uint32_t GetNetId( Entity& InEntity ) const;
    EntityHandle FindEntity( uint32_t InNetId ) const;
    const NetSnapshot& GetCurrent() const { return m_current; }
    // Messages from accepted clients that replication doesn't handle (RPCs).
    std::function<void( NetServer::Client&, uint8_t InType, BitReader& InReader, bool InReliable )> OnOtherMessage;

    // Where a client's view is, for relevancy and priority (otherwise its first owned entity).
    void SetViewer( uint8_t InClient, const Vector3& InPosition ) { m_viewers[InClient] = InPosition; }
    // The tick an entity's state last went to a client (0: never).
    uint32_t GetLastSentTick( uint8_t InClient, uint32_t InNetId ) const;
    uint64_t GetClientSnapshotBytes( uint8_t InClient ) const;
    uint64_t GetClientSnapshotsSent( uint8_t InClient ) const;
    // Snapshot message bytes sent to every client so far.
    uint64_t GetSnapshotBytes() const { return m_snapshotBytes; }
    uint64_t GetSnapshotsSent() const { return m_snapshotsSent; }

private:
    struct ClientState
    {
        std::deque<NetSnapshot> Sent;   // newest last, up to 32
        bool HasAck = false;
        uint32_t AckedTick = 0;
        bool HasSent = false;
        uint32_t LastSentTick = 0;
        double Tokens = 0.0;
        std::map<uint32_t, float> Accumulators;
        std::map<uint32_t, uint32_t> LastSentEntity;
        uint64_t Bytes = 0;
        uint64_t Snapshots = 0;
        // This tick's snapshot, encoded ahead of the packet (on a job).
        std::vector<uint8_t> Prepared;
        NetSnapshot PreparedSent;
        bool HasPrepared = false;
    };
    struct EntityMeta
    {
        uint8_t Owner = 0;
        float Priority = 1.f;
        bool AlwaysRelevant = false;
    };

    void WriteSpawn( BitWriter& InWriter, uint32_t InId, Entity& InEntity ) const;
    void PrepareSnapshots();
    void Prepare( uint8_t InClient, ClientState& InState ) const;
    void OnPacket( NetServer::Client& InClient );
    void OnClientAccepted( NetServer::Client& InClient );

    World& m_world;
    NetServer& m_server;
    NetSnapshot m_current;
    std::map<uint32_t, EntityMeta> m_meta;
    std::map<uint8_t, Vector3> m_viewers;
    std::map<uint32_t, EntityHandle> m_entities;
    std::map<uint32_t, uint8_t> m_sentOwners;   // what clients were last told
    uint32_t m_nextDynamicId = kFirstDynamicId;
    uint32_t m_staticCount = 0;
    std::unordered_map<uint8_t, ClientState> m_clients;
    uint64_t m_snapshotBytes = 0;
    uint64_t m_snapshotsSent = 0;
};


class NetReplicationClient
{
public:
    NetScriptState Scripts;

    NetReplicationClient( World& InWorld, NetClient& InClient );

    void AssignStaticIds();
    // Once a frame: advances the interpolation clock and poses every mirrored entity.
    void Update( double InFrameSeconds );
    // Before the client's packet: acks the newest snapshot.
    void BeforeFlush();

    EntityHandle FindEntity( uint32_t InNetId ) const;
    size_t GetEntityCount() const { return m_entities.size(); }
    // The (fractional) server tick entities are shown at.
    double GetRenderTick() const { return m_renderTick; }
    double GetDelayTicks() const { return m_delay; }
    // NET-15, high ping: the oldest render tick the server will still rewind a shot to (the next
    // command's tick less its rewind cap). A render tick older than that jumps forward to it, and
    // remote movers past their newest sample dead-reckon from their last two, up to
    // MaxExtrapolationSeconds. < 0: no limit.
    void SetMinRenderTick( double InTick ) { m_minRenderTick = InTick; }
    bool IsExtrapolating() const { return m_extrapolating; }
    double MaxExtrapolationSeconds = 0.25;
    uint32_t GetNewestSnapshotTick() const { return m_newest; }
    uint64_t GetSnapshotsReceived() const { return m_received; }

    // The server's state of a character this client owns (NET-4), with its [Predicted] script
    // bytes (NET-6): for its predictor.
    std::function<void( uint32_t InNetId, uint32_t InTick, const CharacterController::NetState& InState, const std::vector<uint8_t>& InScripts )> OnOwnerState;
    // An entity's owner changed (or arrived with its spawn).
    std::function<void( uint32_t InNetId, uint8_t InOwner )> OnOwnerChanged;
    // Messages from the server that replication doesn't handle (RPCs).
    std::function<void( uint8_t InType, BitReader& InReader, bool InReliable )> OnOtherMessage;
    // Entities this client predicts aren't interpolated.
    void SetPredicted( uint32_t InNetId ) { m_predicted.insert( InNetId ); }
    // Back to following the server's snapshots (ownership moved away).
    void ClearPredicted( uint32_t InNetId ) { m_predicted.erase( InNetId ); }

private:
    struct Sample
    {
        uint32_t Tick = 0;
        NetEntityState State;
    };

    void OnMessage( uint8_t InType, BitReader& InReader, bool InReliable );
    void OnSpawn( BitReader& InReader );
    void OnDespawn( BitReader& InReader );
    void OnSnapshot( BitReader& InReader );
    void Bind( uint32_t InId, EntityHandle InEntity );

    World& m_world;
    NetClient& m_client;
    std::map<uint32_t, EntityHandle> m_entities;
    std::map<uint32_t, std::deque<Sample>> m_samples;
    std::set<uint32_t> m_predicted;
    std::deque<NetSnapshot> m_snapshots;    // decoded, newest last, up to 32
    bool m_hasSnapshot = false;
    uint32_t m_newest = 0;
    uint64_t m_received = 0;
    bool m_clockStarted = false;
    double m_renderTick = 0.0;
    double m_delay = 2.0;
    double m_settleSeconds = 0.0;
    double m_minRenderTick = -1.0;
    bool m_extrapolating = false;
};
