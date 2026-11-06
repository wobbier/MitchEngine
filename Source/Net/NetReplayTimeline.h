#pragma once
#include "Net/NetReplay.h"
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

// A match replay's timeline (ME-316 phase 3): one pass over its ticks gives the moments worth
// jumping to and each client's link along the match, for the viewer's scrubber and graphs and for
// ReplayDump's --markers. Nothing here needs the engine running.
//
// Markers, in tick order:
// - kill:   an event named "kill" (the game's own, C#'s Replay.Event; the convention HeroShooter uses);
// - join / leave: net.joined / net.left;
// - event:  any other event (picks, damage, respawns...);
// - hit:    a lag-compensated trigger pull that hit (not a projectile's sweep);
// - miss:   a client's command hadn't arrived and the server repeated the last one: where its
//           prediction is most likely to be corrected (the server records no client corrections);
//           misses within 10 ticks of each other are one marker (Ticks: its span);
// - resync: a client's clock went back (its command buffer rewound: NetCommandBuffer resyncs);
// - hitch:  the server was behind (ticks owed after a tick) or dropped backlog; within 30 ticks of
//           each other, one marker.
struct NetReplayMarker
{
    enum class Kind : uint8_t
    {
        Kill,
        Join,
        Leave,
        Event,
        Hit,
        Miss,
        Resync,
        Hitch,
        Count
    };
    uint32_t Tick = 0;
    Kind Type = Kind::Event;
    std::string Name;           // the event's name ("kill", "pick"); the kind's for the rest
    std::string Label;          // one line: what happened, with names ("Bravo's Bruiser#65539 hit Striker#65537 (Head)")
    uint32_t Entity = 0;        // the NetId it's about (the event's, the shooter, ...; 0: none)
    uint8_t Client = 0;         // the client it's about (join / leave / miss / resync / a shot's)
    uint32_t Ticks = 1;         // a run's length (miss, hitch)
};


// One client's link along the timeline, a value per tick from FirstTick to the timeline's LastTick
// (NaN where the client wasn't connected).
struct NetReplayClientSeries
{
    uint8_t Client = 0;
    uint32_t FirstTick = 0;
    uint32_t LastTick = 0;      // its last recorded tick
    std::vector<float> RoundTripMs;
    std::vector<float> PacketLoss;  // 0..1
    std::vector<float> Slack;       // ticks
    std::vector<uint8_t> Missed;    // 1 where the server repeated its command
    uint32_t Misses = 0;            // totals, as of its last tick
    uint32_t LateCommands = 0;
    uint32_t Resyncs = 0;
    float MaxRoundTripMs = 0.f;
    float MaxSlack = 0.f;
};


class NetReplayTimeline
{
public:
    static const char* KindName( NetReplayMarker::Kind InKind );
    // "kill", "join", ... -> the kind; false when it isn't one.
    static bool ParseKind( const std::string& InName, NetReplayMarker::Kind& OutKind );

    // One pass over InReader's ticks. InCancel (optional) stops it early (false, no error). False
    // with OutError when a chunk won't decode.
    bool Build( NetReplayReader& InReader, std::string* OutError = nullptr, const std::atomic<bool>* InCancel = nullptr );

    uint32_t GetFirstTick() const { return m_first; }
    uint32_t GetLastTick() const { return m_last; }
    const std::vector<NetReplayMarker>& GetMarkers() const { return m_markers; }
    const std::vector<NetReplayClientSeries>& GetClients() const { return m_clients; }
    // Per tick from the first: ticks owed after it, and its trigger pulls (not sweeps).
    const std::vector<uint32_t>& GetTicksOwed() const { return m_owed; }
    const std::vector<uint16_t>& GetShots() const { return m_shots; }
    size_t Count( NetReplayMarker::Kind InKind ) const;

    // The index of the InNth marker of InKind (0 the first, -1 the last, -2 the one before...);
    // InName narrows to an event's name ("" any). -1 when there's none.
    int Find( NetReplayMarker::Kind InKind, int InNth, const std::string& InName = "" ) const;
    // The first marker after InTick (InForward) or before it, of a kind InMask allows (bit per
    // Kind); -1 when there's none.
    int Neighbour( uint32_t InTick, bool InForward, uint32_t InMask = ~0u ) const;

private:
    uint32_t m_first = 0;
    uint32_t m_last = 0;
    std::vector<NetReplayMarker> m_markers;
    std::vector<NetReplayClientSeries> m_clients;
    std::vector<uint32_t> m_owed;
    std::vector<uint16_t> m_shots;
};
