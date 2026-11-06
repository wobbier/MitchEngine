#pragma once
#include "Net/NetReplay.h"
#include "Net/NetSnapshot.h"
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

class NetReplicationClient;
struct NetScriptState;

// Match replay playback (ME-316 phase 2): a replay source's ticks played into a world through the
// client's own replication path. Each tick becomes the messages a server would have sent for it
// (NetReplicationMessages: spawns, despawns, owner changes, a snapshot delta against the tick
// before) and goes to NetReplicationClient::Deliver, so spawning from prefabs, the snapshot codec,
// interpolation, NetSchema and the C# [Replicated] fields all run as they do on a client. Nothing
// is predicted (the viewer owns nothing), and the client's render tick is driven: the world shows
// exactly the tick the playback is at, fractional between ticks.
//
// Controls: play / pause, step +-N ticks, seek to any tick (decodes its chunk from the keyframe and
// sends that tick whole, after the spawns and despawns that make the world's entities match it),
// speed 0.1x to 4x, a loop range, and a pause-at tick (tests, --replay-pause-at).
//
// It holds no engine globals: the source, the client and the world are the caller's (NetCore's in
// the Player; a test's own), so a replay world of its own (ME-297) is a move, not a rewrite.
class NetReplayPlayback
{
public:
    static constexpr double kMinSpeed = 0.1;
    static constexpr double kMaxSpeed = 4.0;

    // InSource and InClient must outlive it. Starts paused at the first tick.
    NetReplayPlayback( INetReplaySource& InSource, NetReplicationClient& InClient );

    const NetReplayHeader& GetHeader() const { return m_reader.GetHeader(); }
    bool IsEmpty() const { return m_reader.IsEmpty(); }
    uint32_t GetFirstTick() const { return m_first; }
    // The last tick the source has (a streaming one's grows as its chunks arrive).
    uint32_t GetLastTick() const { return m_last; }

    // ME-323 phase 3: a source still being filled (a killcam's NetReplayMemorySource while its chunks
    // stream in). Each Update picks up the chunks added since, and playing onto the last tick there is
    // waits on it (still playing, the playhead held) instead of pausing; streaming off, it pauses there
    // as at the end of any replay. A source only ever grows at its end.
    void SetStreaming( bool InStreaming ) { m_streaming = InStreaming; }
    bool IsStreaming() const { return m_streaming; }
    // Frames a streaming playback spent held on its last tick, waiting for the next chunk.
    uint64_t GetStalledFrames() const { return m_stalledFrames; }
    uint16_t GetTickRate() const;

    void Play();
    void Pause();
    void TogglePause() { m_paused ? Play() : Pause(); }
    bool IsPaused() const { return m_paused; }
    // Clamped to kMinSpeed..kMaxSpeed.
    void SetSpeed( double InSpeed );
    double GetSpeed() const { return m_speed; }
    // Shows InTick (clamped to the replay) from the next Update, playing or paused as before.
    void Seek( double InTick );
    // Pauses and moves InTicks whole ticks from the tick shown (negative: back).
    void Step( int64_t InTicks );
    // Playing past InTo goes back to InFrom (clamped, swapped if reversed).
    void SetLoop( uint32_t InFrom, uint32_t InTo );
    void ClearLoop() { m_looping = false; }
    bool IsLooping() const { return m_looping; }
    uint32_t GetLoopFrom() const { return m_loopFrom; }
    uint32_t GetLoopTo() const { return m_loopTo; }
    // Playing forward onto or past InTick pauses there, once (< 0: none; past the end: the last tick,
    // so the end of the replay is where it rests either way).
    void SetPauseAt( int64_t InTick );
    int64_t GetPauseAt() const { return m_pauseAt; }

    // Every frame, before the client's Update: moves the playhead (playing: InFrameSeconds x the
    // tick rate x speed ticks; reaching the end pauses there), then feeds the client what showing it
    // takes and drives its render tick. False when the replay couldn't be read (GetError).
    bool Update( double InFrameSeconds );

    // The tick shown (fractional while playing) and its whole tick.
    double GetTime() const { return m_time; }
    uint32_t GetTick() const { return static_cast<uint32_t>( m_time ); }
    // The whole tick's record (commands, shots, events, movers) as of the last Update; null when the
    // replay has none for it.
    const NetReplayTick* GetRecord() const { return m_hasRecord ? &m_record : nullptr; }
    // Between two ticks: the record of the tick after the one shown (the one the client interpolates
    // towards), for presenting what snapshots don't carry (a killcam's recorded aim) between ticks
    // too; null on a whole tick or when it has none.
    const NetReplayTick* GetNextRecord() const { return m_hasNextRecord && m_nextRecord.Tick == GetTick() + 1 ? &m_nextRecord : nullptr; }
    // What each NetId is (name, prefab, owner) as of the tick fed last.
    const std::map<uint32_t, NetReplayEntity>& GetEntityTable() const { return m_table; }
    NetReplayReader& GetReader() { return m_reader; }
    const std::string& GetError() const { return m_error; }

    // Ticks fed one after another, and rebuilds (seeks back, jumps, loops) so far.
    uint64_t GetTicksFed() const { return m_ticksFed; }
    uint64_t GetRebuilds() const { return m_rebuilds; }

    // The world against the recording at the tick shown (call after the client's Update, paused on a
    // whole tick): each recorded entity's state as the server would capture it from the world now
    // (NetReplicationState::Capture with InScripts), and the sample the client showed, compared with
    // the recorded state. Positions, fields and the owner (NetIdentity against the recording's) must
    // match exactly; a re-captured rotation within one step of its 12-bit grid (decoding and
    // encoding the smallest three can land a step over).
    struct Check
    {
        uint32_t Tick = 0;
        size_t Entities = 0;            // recorded at the tick
        size_t Matched = 0;
        std::vector<std::string> Problems;  // one line each ("Striker#65537: position ...")
        bool Passed() const { return Problems.empty() && Entities > 0; }
    };
    Check CheckWorld( const NetScriptState* InScripts = nullptr ) const;

private:
    // Makes the client show m_time: feeds ticks on from the last one fed, or rebuilds from the tick.
    bool Present();
    // A streaming source's new chunks: the last tick moves on.
    void Refresh();
    bool Rebuild( uint32_t InTick );
    void Feed( const NetReplayTick& InTick );
    void Send( const class BitWriter& InMessage, bool InReliable );
    // The record's entity changes, spawns, despawns and owners against what the client has.
    void SyncEntities( const NetReplayTick& InTick, const NetSnapshot* InBefore );

    NetReplayReader m_reader;
    NetReplicationClient& m_client;
    uint32_t m_first = 0;
    uint32_t m_last = 0;
    double m_time = 0.0;
    double m_speed = 1.0;
    bool m_paused = true;
    bool m_looping = false;
    uint32_t m_loopFrom = 0;
    uint32_t m_loopTo = 0;
    int64_t m_pauseAt = -1;
    // What the client has: the last tick fed and its snapshot (the next delta's baseline), the
    // first tick fed since the last rebuild, and what each NetId is.
    bool m_fed = false;
    uint32_t m_fedTick = 0;
    uint32_t m_historyStart = 0;
    uint32_t m_lastShown = 0;
    NetSnapshot m_fedState;
    std::map<uint32_t, NetReplayEntity> m_table;
    NetReplayTick m_record;
    bool m_hasRecord = false;
    NetReplayTick m_nextRecord;
    bool m_hasNextRecord = false;
    bool m_streaming = false;
    size_t m_chunks = 0;
    uint64_t m_stalledFrames = 0;
    std::string m_error;
    uint64_t m_ticksFed = 0;
    uint64_t m_rebuilds = 0;
    bool m_warnedStatic = false;
};
