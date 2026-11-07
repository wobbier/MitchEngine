#pragma once
#include "Net/NetReplay.h"
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

class BitReader;
class BitWriter;

// Runtime net debugging (NET-7, the Overwatch hit-registration comparison and the bandwidth view).
// Nothing here runs unless a session asks for it:
//
// - A client asks the server (NetMessage::NetDebugSubscribe, reliable) for what its Net Debugger
//   shows: Shots and / or Bandwidth. It asks once connected and again whenever that changes.
// - Shots: for each lag-compensated ray its pawn fires with a command (NetCore::LagCompensatedRaycast
//   for an entity the client owns, not a projectile's sweep), the server sends the client its
//   judgement (NetMessage::NetDebug, kind Shot, reliable): the command's tick, the shot as the replay
//   records it (hit or miss, the point, the view tick and how far it rewound, the target) with the
//   rewound poses of the target's hitboxes (or, on a miss, of whatever networked entity the ray
//   passed nearest within kNearMissMetres), and the same hitboxes where they were live. The client
//   records its own shots from its predicted pawn (NetCore::PresentRaycast inside a tick, not while
//   resimulating) and pairs each result with the one it predicted (NetShotLedger): green for its
//   view, yellow / blue for the server's, drawn for kShowSeconds.
// - Bandwidth: once a second the server sends the client the entities that took the most of its
//   snapshot bytes over that second (NetMessage::NetDebug, kind Bandwidth), counted bit by bit by
//   the snapshot writer (NetSnapshotCodec::Write's per-entity accounting).
//
// Retail builds compile it out: their servers ignore a subscription and their clients never send
// one (NetDebug::kAvailable).
namespace NetDebug
{
#if USING( ME_RETAIL )
    constexpr bool kAvailable = false;
#else
    constexpr bool kAvailable = true;
#endif
    // A shot's result and the overlays drawn for it stay this long.
    constexpr double kShowSeconds = 2.0;
    // A miss names the networked entity the ray passed nearest, this close.
    constexpr float kNearMissMetres = 2.f;
    // Hitboxes a result carries, each way (rewound and live).
    constexpr size_t kMaxHitboxes = 6;
    // Entities a bandwidth report names.
    constexpr size_t kTopEntities = 8;

    enum Kind : uint8_t
    {
        Shot = 1,
        Bandwidth = 2,
    };
}

struct NetDebugSubscription
{
    bool Shots = false;
    bool Bandwidth = false;
    bool Any() const { return Shots || Bandwidth; }
    bool operator==( const NetDebugSubscription& InOther ) const { return Shots == InOther.Shots && Bandwidth == InOther.Bandwidth; }
    bool operator!=( const NetDebugSubscription& InOther ) const { return !( *this == InOther ); }
};

// What the server judged one shot to be (it sends it to the shooter).
struct NetShotResult
{
    uint32_t CommandTick = 0;           // the shooter's command it was fired with (the client's tick)
    NetReplayShot Shot;                 // Hitboxes: the target's (or the near miss's), rewound to the view
    std::vector<NetReplayHitbox> Live;  // the same hitboxes at the server's newest tick
};

// One entity's share of the snapshot bytes.
struct NetEntityBytes
{
    uint32_t NetId = 0;
    uint64_t Bits = 0;
    double BytesPerSecond = 0.0;
    std::string Name;                   // filled in by whoever shows it
};

// The entities that took the most of one client's snapshot bytes over Seconds.
struct NetBandwidthReport
{
    uint32_t Tick = 0;                  // the server tick it ends at
    double Seconds = 0.0;
    uint64_t SnapshotBytes = 0;         // every snapshot sent to the client over it
    uint64_t OverheadBits = 0;          // what no entity's state took (headers, gone lists, padding)
    std::vector<NetEntityBytes> Top;    // most first, at most NetDebug::kTopEntities
};

namespace NetDebugMessages
{
    // Each with its type byte first.
    void WriteSubscribe( BitWriter& InWriter, const NetDebugSubscription& InSubscription );
    void WriteShot( BitWriter& InWriter, const NetShotResult& InResult );
    void WriteBandwidth( BitWriter& InWriter, const NetBandwidthReport& InReport );
    // After the type byte.
    bool ReadSubscribe( BitReader& InReader, NetDebugSubscription& OutSubscription );
    // OutKind says which of the two it filled.
    bool ReadDebug( BitReader& InReader, uint8_t& OutKind, NetShotResult& OutShot, NetBandwidthReport& OutBandwidth );
}

// A shot the client fired from its predicted pawn, as its own world saw it.
struct NetPredictedShot
{
    uint32_t Tick = 0;                  // its command's
    uint32_t Shooter = 0;               // NetId
    Vector3 Origin;
    Vector3 Direction;                  // unit
    float Range = 0.f;
    double ViewTick = -1.0;             // the render tick its command names
    bool Hit = false;
    bool Occluded = false;
    uint32_t Target = 0;                // NetId of the hitbox's networked entity
    std::string TargetPart;
    Vector3 Point;
    float Distance = 0.f;
    std::vector<NetReplayHitbox> Hitboxes;  // the target's (or the near miss's) as the client showed them
};

// The client's shots and the server's results, paired (NET-7).
struct NetShotComparison
{
    bool HasClient = false;
    bool HasServer = false;
    NetPredictedShot Client;
    NetShotResult Server;
    double At = 0.0;                    // when it was settled (paired, or given up on)

    // Paired, and both hit the same networked entity, or both missed (an occluded shot is a miss).
    bool Agrees() const;
    // Both hit the same part of it too.
    bool SamePart() const;
    // Between the two hit points (m); < 0 unless both hit.
    float PointDelta() const;
    // "agree: hit #3 Head, 12.4 mm apart" / "client hit #3, server missed (occluded)" / "server only: miss".
    std::string Describe() const;
};

class NetShotLedger
{
public:
    // A predicted shot with no result this long after it was fired settles as client-only.
    static constexpr double kPendingSeconds = 3.0;
    static constexpr size_t kMaxRecent = 64;
    // How close a result's ray must be to a predicted one to pair with it (same tick and shooter).
    static constexpr float kMaxOriginDelta = 0.5f;      // m
    static constexpr float kMinDirectionDot = 0.999f;

    struct Totals
    {
        uint64_t Predicted = 0;
        uint64_t Results = 0;
        uint64_t Paired = 0;
        uint64_t Agreed = 0;
        uint64_t SamePart = 0;
        uint64_t Disagreed = 0;
        uint64_t ClientOnly = 0;        // never answered (lost, a server without the debug message)
        uint64_t ServerOnly = 0;        // the server judged a shot the client didn't predict
        uint64_t PointPairs = 0;        // pairs where both hit
        double SumPointDelta = 0.0;
        double MaxPointDelta = 0.0;
    };

    void AddPredicted( NetPredictedShot InShot, double InNow );
    // Pairs it with the closest predicted ray of its tick and shooter (none: server only).
    const NetShotComparison& AddResult( NetShotResult InResult, double InNow );
    // Settles predicted shots past kPendingSeconds.
    void Update( double InNow );
    void Reset();

    // Settled, newest last.
    const std::deque<NetShotComparison>& GetRecent() const { return m_recent; }
    size_t GetPendingCount() const { return m_pending.size(); }
    const Totals& GetTotals() const { return m_totals; }

private:
    struct Pending
    {
        NetPredictedShot Shot;
        double At = 0.0;
    };
    void Settle( NetShotComparison InComparison );
    std::deque<Pending> m_pending;
    std::deque<NetShotComparison> m_recent;
    Totals m_totals;
};
