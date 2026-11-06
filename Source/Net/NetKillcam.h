#pragma once
#include "Net/NetReplay.h"
#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

// Killcams (ME-323 phase 2): the last seconds before a death as the killer saw them, cut from the
// server's replay ring (NetReplayRecorder::GetRing, the same per-tick capture the .mreplay file is
// written from: nothing is captured twice) and streamed to the victim over the blob channel.
//
// The stream is the ME-316 chunk format, filtered for the killer:
//
// - State: every entity the tick has, as the killer's client had it. Those in its view (NET-13's
//   relevancy: within RelevantRadius of the killer's pawn, its state's position that tick;
//   AlwaysRelevant ones; the ones its client owns; the killer itself; plus the victim, whose death
//   it shows, however far) with their state that tick. The rest hold the last state it had of them,
//   as a client does (spawns and despawns go to every client whatever the view): their last in
//   view, or the one they first have in the ring (the spawn pose, for one spawned since). So an
//   entity leaves State only when it's destroyed, and a playback never despawns one for leaving view.
// - Clients: the killer's client's command (buttons, axes, aim, ViewTick) and link; none for a bot.
// - Movers: the killer's own (full precision and [Predicted] bytes), for its camera.
// - Shots: those fired by, through (instigator) or at a relevant entity, with only the relevant
//   hitboxes (and the unnetworked ones).
// - Events: those about a relevant entity, or none in particular.
// - Entities: a chunk's first tick names every entity in it; later ticks name the ones that come
//   into view and owner changes, so each chunk decodes alone.
//
// Blobs (NetConnection::SendBlob), one a chunk, each with a small prefix:
//
//   u32 "MRKC", u8 stream version, u32 killcam id, u16 chunk index, u16 chunk count, u32 total bytes
//   chunk 0 only:  u32 victim NetId, u32 killer NetId, u8 killer's client, u32 death tick,
//                  u32 first tick, u32 header size, NetReplayFile::EncodeHeader bytes
//   then the chunk (NetReplayChunk::Encode, header included)
//
// The receiver (NetKillcamReceiver) puts chunks into a NetReplayMemorySource in order as they
// complete (any order on the wire), so a reader can start on the first chunk while the rest stream.
namespace NetKillcamFormat
{
    constexpr uint32_t kMagic = 0x434B524Du;   // "MRKC"
    constexpr uint8_t kVersion = 1;
    constexpr size_t kPrefixBytes = 17;
    constexpr size_t kInfoBytes = 21;           // chunk 0's, before the header bytes
    constexpr uint32_t kMaxChunks = 1024;
}


// What a killcam is cut from and for.
struct NetKillcamRequest
{
    uint32_t Id = 0;                // the server's, from 1: a newer one replaces an older on the client
    uint32_t VictimNetId = 0;
    uint32_t KillerNetId = 0;
    uint32_t FirstTick = 0;
    uint32_t DeathTick = 0;         // the last tick sent
    // NET-13's settings (NetReplicationSettings::RelevantRadius; 0: everything is relevant) and the
    // NetIds that are AlwaysRelevant: every one that was at a tick in the ring, destroyed since or
    // not (NetCore keeps them as it records), so a projectile gone before the cut still counts.
    float RelevantRadius = 0.f;
    std::set<uint32_t> AlwaysRelevant;
    uint32_t ChunkTicks = 60;       // ticks a chunk (a keyframe and its deltas): a blob each
    NetReplayCodec Codec = NetReplayCodec::Deflate;
    NetReplayHeader Header;         // the recording's (its tick rate, protocol and hashes)
};


namespace NetKillcam
{
    // The killcam's ticks, FirstTick..DeathTick, filtered for the killer (above). InRing: the
    // recorder's ring, oldest first; InRingEntities: what every NetId was as of its oldest tick
    // (NetReplayRecorder::GetRingEntities). OutError says why it's empty (the range isn't in the
    // ring, the killer is in none of its ticks).
    std::vector<NetReplayTick> Filter( const NetKillcamRequest& InRequest, const std::vector<std::shared_ptr<const NetReplayTick>>& InRing,
        const std::map<uint32_t, NetReplayEntity>& InRingEntities, std::string* OutError = nullptr );

    struct Encoded
    {
        std::vector<std::vector<uint8_t>> Blobs;   // in order: chunk 0 (with the header) first
        uint32_t FirstTick = 0;
        uint32_t LastTick = 0;
        uint32_t Ticks = 0;
        size_t Bytes = 0;           // every blob's
        size_t RawBytes = 0;        // the chunks' before deflate
        double Milliseconds = 0.0;  // filtering and encoding took
        std::string Error;          // nothing to send, and why
    };
    // Filter, then chunks of InRequest.ChunkTicks, each a blob. Reads only its arguments (the ring's
    // ticks are immutable): runs on the background lane.
    Encoded Encode( const NetKillcamRequest& InRequest, const std::vector<std::shared_ptr<const NetReplayTick>>& InRing,
        const std::map<uint32_t, NetReplayEntity>& InRingEntities );

    // A blob's prefix. False when it isn't a killcam's.
    struct Prefix
    {
        uint32_t Id = 0;
        uint16_t Index = 0;
        uint16_t Count = 0;
        uint32_t TotalBytes = 0;
    };
    bool ReadPrefix( const uint8_t* InData, size_t InSize, Prefix& OutPrefix );
}


// A killcam's replay source as a playback reads it (ME-323 phase 3, the "replay transport"): the
// receiver's NetReplayMemorySource, shared, so NetCore::StartPlayback can own a source while the
// receiver keeps adding the chunks still streaming in (NetReplayPlayback::SetStreaming). It keeps the
// source alive after the receiver has moved on to a newer killcam.
class NetKillcamSource final
    : public INetReplaySource
{
public:
    explicit NetKillcamSource( std::shared_ptr<NetReplayMemorySource> InSource ) : m_source( std::move( InSource ) ) {}
    const NetReplayHeader& GetHeader() const override { return m_source->GetHeader(); }
    size_t GetChunkCount() const override { return m_source->GetChunkCount(); }
    const NetReplayChunk::Info& GetChunkInfo( size_t InIndex ) const override { return m_source->GetChunkInfo( InIndex ); }
    bool ReadChunk( size_t InIndex, std::vector<uint8_t>& OutBytes ) override { return m_source->ReadChunk( InIndex, OutBytes ); }

private:
    std::shared_ptr<NetReplayMemorySource> m_source;
};


// The client's side: a killcam's blobs back into a replay source.
class NetKillcamReceiver
{
public:
    enum class Result
    {
        NotKillcam,     // another kind of blob
        Accepted,       // a chunk of the current killcam (or the first of a newer one)
        Stale,          // an older killcam's, or a chunk already in
        Bad,            // a killcam's that doesn't read
    };
    Result Accept( const uint8_t* InData, size_t InSize );
    void Reset();

    // 0: none yet.
    uint32_t GetId() const { return m_id; }
    bool HasHeader() const { return m_source != nullptr; }
    bool IsComplete() const { return m_count > 0 && m_ready == m_count; }
    uint32_t GetChunkCount() const { return m_count; }
    // Chunks in the source (those before them all arrived; later ones may be waiting).
    uint32_t GetChunksReady() const { return m_ready; }
    uint32_t GetChunksReceived() const { return m_received; }
    size_t GetBytesReceived() const { return m_bytes; }
    size_t GetTotalBytes() const { return m_totalBytes; }
    uint32_t GetVictimNetId() const { return m_victim; }
    uint32_t GetKillerNetId() const { return m_killer; }
    uint8_t GetKillerClient() const { return m_killerClient; }
    uint32_t GetDeathTick() const { return m_deathTick; }
    uint32_t GetFirstTick() const { return m_firstTick; }
    // The replay so far (null until chunk 0 is in): an INetReplaySource for NetReplayReader or
    // playback. Kept until the next killcam or Reset.
    std::shared_ptr<NetReplayMemorySource> GetSource() const { return m_source; }
    const std::string& GetError() const { return m_error; }

private:
    void Drain();

    uint32_t m_id = 0;
    uint32_t m_count = 0;
    uint32_t m_ready = 0;
    uint32_t m_received = 0;
    size_t m_bytes = 0;
    size_t m_totalBytes = 0;
    uint32_t m_victim = 0;
    uint32_t m_killer = 0;
    uint8_t m_killerClient = 0;
    uint32_t m_deathTick = 0;
    uint32_t m_firstTick = 0;
    std::shared_ptr<NetReplayMemorySource> m_source;
    std::map<uint16_t, std::vector<uint8_t>> m_waiting;     // chunks ahead of the source
    std::set<uint16_t> m_have;
    std::string m_error;
};
