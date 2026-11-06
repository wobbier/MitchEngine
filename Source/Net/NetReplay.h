#pragma once
#include "Components/Physics/CharacterController.h"
#include "Math/Quaternion.h"
#include "Math/Vector3.h"
#include "Net/NetCommands.h"
#include "Net/NetSnapshot.h"
#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace Jobs
{
    class SerialQueue;
}

// Match replays (ME-316; the file half of ME-202): the server's ground truth for a whole match,
// one record per simulated tick, for debugging after the fact. Recorded on the server (dedicated
// or listen) after each tick's CaptureTick, with no relevancy filter:
//
// - every networked entity's NetEntityState (the quantized pose and NetSchema bytes, the same
//   NetSnapshot replication captures);
// - each client's command for the tick (buttons, axes, aim, ViewTick), whether it was a repeat,
//   and its link (RTT, loss, command slack, misses, late commands, clock resyncs);
// - the tick's owed ticks and the backlog the engine dropped before it;
// - what each NetId is (name, prefab, owner) when it appears or changes owner;
// - every networked character's mover at full precision and its [Predicted] script state (what
//   OwnerState sends the owner: a resimulation starts from it);
// - every lag-compensated shot: shooter (and who fired it, for a projectile's sweep), ray, view
//   tick, rewind, hit / miss / occluded / clamped, and the hitboxes as they were rewound;
// - events: joins and leaves, and whatever the game writes (C#'s Replay.Event: kills, picks).
//
// The .mreplay file (little-endian, versioned):
//
//   "MREPLAY\0", u32 format version, u32 header size, header (bit stream, NetReplayHeader)
//   chunk*                  self-contained: a keyframe tick (the full state) then deltas
//   index                   "MRIX", u32 count, {u64 offset, u32 first tick, u32 last tick, u32 ticks}*
//   trailer                 u64 index offset, "MRINDEX\0"
//
// A chunk is "MRCK", u32 first tick, u32 last tick, u32 ticks, u8 codec, 3 bytes 0, u32 raw size,
// u32 stored size, u64 FNV-1a of the stored bytes, then the stored bytes (deflated with
// NetReplayCodec::Deflate). Its states are NetSnapshotCodec deltas against the tick before, the
// first in full, and its first tick names every live NetId, so a chunk decodes alone: seeking
// decodes one chunk, and the same bytes can go over a network blob channel (ME-323's killcam).
// A file whose server died has no index: readers scan the chunks instead and stop at the first
// torn one. While recording, the open chunk is also written every FlushSeconds as a provisional
// chunk after the whole ones (an ordinary chunk with fewer ticks, overwritten by the next), so a
// crash loses about a second, not a whole chunk.
namespace NetReplayFormat
{
    // 2: link stats at their resolution (RTT 0.1 ms, loss 0.01%, slack 0.01 tick), sent when they change.
    // 3: movers (CharacterController::NetState + [Predicted] bytes), shot instigators, a shot's
    //    hitboxes shared with an earlier shot of the tick that had the same ones.
    // Readers take kMinVersion..kVersion (a chunk decodes with its file's version).
    constexpr uint32_t kVersion = 3;
    constexpr uint32_t kMinVersion = 2;
    constexpr char kMagic[8] = { 'M', 'R', 'E', 'P', 'L', 'A', 'Y', '\0' };
    constexpr char kTrailer[8] = { 'M', 'R', 'I', 'N', 'D', 'E', 'X', '\0' };
    constexpr uint32_t kChunkMagic = 0x4B43524Du;   // "MRCK"
    constexpr uint32_t kIndexMagic = 0x5849524Du;   // "MRIX"
    constexpr size_t kChunkHeaderBytes = 36;
    // Script payloads (Replay.Event) are cut to this; names to kMaxEventName.
    constexpr size_t kMaxEventPayload = 4000;
    constexpr size_t kMaxEventName = 64;
}

enum class NetReplayCodec : uint8_t
{
    Raw = 0,
    Deflate = 1,    // zlib
};

struct NetReplayHeader
{
    uint32_t FormatVersion = NetReplayFormat::kVersion;
    std::string Product;        // "<ProductName> <ProductVersion>"
    std::string EngineCommit;   // the engine checkout's HEAD when recorded ("" when unknown: a shipped build)
    std::string GameCommit;     // the project's
    std::string Map;            // the scene
    std::string CommandLine;
    std::string RecordedAt;     // UTC, ISO 8601
    uint16_t TickRate = 60;
    uint32_t KeyframeInterval = 0;
    // The handshake's NetVersion (NET-2) and the reflected NetSchema (NetSchema::Hash): a reader
    // knows which build's bytes these are.
    uint32_t NetProtocol = 0;
    uint64_t BuildHash = 0;
    uint64_t ScriptSchemaHash = 0;
    uint64_t SceneHash = 0;
    uint64_t NetSchemaHash = 0;
};

// One client's tick on the server.
struct NetReplayClient
{
    uint8_t Id = 0;
    InputCommand Command;       // what the server simulated (client->Current)
    bool Missed = false;        // it hadn't arrived: a repeat (neutral past the repeat cap)
    // The link, kept to RTT 0.1 ms, loss 0.01% and slack 0.01 tick (a reader gets those back).
    float RoundTripMs = 0.f;
    float PacketLoss = 0.f;
    float Slack = 0.f;          // the command buffer's smoothed slack, in ticks
    uint32_t Misses = 0;        // so far
    uint32_t LateCommands = 0;  // so far
    uint32_t Resyncs = 0;       // the client's clock went back, so far (NetCommandBuffer::GetRewindCount)
};

// A hitbox as a lag-compensated shot was judged against it.
struct NetReplayHitbox
{
    uint32_t NetId = 0;         // its entity's, or its nearest networked ancestor's
    std::string Name;           // its entity's ("Head")
    uint8_t Shape = 0;          // NetHitboxShape
    Vector3 Position;
    Quaternion Rotation;
    float Radius = 0.f;
    float HalfHeight = 0.f;
    Vector3 HalfExtents;
    bool Occluder = false;
};

struct NetReplayShot
{
    uint8_t Client = 0;         // the owner of whoever fired it (0: the server's own, a bot)
    uint32_t Shooter = 0;       // NetId (0: not networked): the entity the ray is cast for
    // Who fired it, when the shooter is a projectile sweeping its flight (0: the shooter itself, a
    // trigger pull). Client is the instigator's owner then.
    uint32_t Instigator = 0;
    Vector3 Origin;
    Vector3 Direction;
    float Range = 0.f;
    double ViewTick = -1.0;     // what the command named (< 0: none, judged at the newest tick)
    double RewoundTicks = 0.0;  // judged at newest recorded tick - this
    bool Hit = false;
    bool Occluded = false;
    bool Clamped = false;
    bool Rejected = false;
    uint32_t Target = 0;        // the hitbox's NetId (hit, or hidden when Occluded)
    std::string TargetPart;     // the hitbox entity's name
    Vector3 Point;
    float Distance = 0.f;
    uint32_t Tested = 0;
    std::vector<NetReplayHitbox> Hitboxes;  // every hitbox at the judged tick (not the shooter's)
};

// A networked character's mover after the tick, at full precision, and its [Predicted] script
// state: what the server sends the owner (OwnerState) to restore from. A resimulation seeds from it.
struct NetReplayMover
{
    uint32_t NetId = 0;
    CharacterController::NetState State;
    std::vector<uint8_t> Predicted;     // NetScriptState::SavePredicted's bytes ("" without scripts)
};

struct NetReplayEvent
{
    std::string Name;           // "net.joined", "net.left", or the game's ("kill", "pick")
    std::string Payload;        // free text; JSON by convention
    uint32_t Entity = 0;        // the NetId it's about (0: none)
};

// What a NetId is.
struct NetReplayEntity
{
    uint32_t NetId = 0;
    std::string Name;
    std::string Prefab;
    uint8_t Owner = 0;
};

struct NetReplayTick
{
    uint32_t Tick = 0;
    uint32_t TicksOwed = 0;     // fixed steps still due after this one (a server catching up)
    uint32_t TicksDropped = 0;  // backlog the engine dropped since the tick before
    NetSnapshot State;          // State.Tick == Tick
    std::vector<NetReplayClient> Clients;
    std::vector<NetReplayMover> Movers;     // every networked character's, by NetId (format 3)
    std::vector<NetReplayShot> Shots;
    std::vector<NetReplayEvent> Events;
    // NetIds that appeared or changed owner this tick; a chunk's first tick lists every live one.
    std::vector<NetReplayEntity> Entities;
};


namespace NetReplayChunk
{
    // One chunk's bytes, header included: the first tick in full, the rest as deltas.
    std::vector<uint8_t> Encode( const std::vector<const NetReplayTick*>& InTicks, NetReplayCodec InCodec, size_t* OutRawBytes = nullptr );
    std::vector<uint8_t> Encode( const std::vector<NetReplayTick>& InTicks, NetReplayCodec InCodec, size_t* OutRawBytes = nullptr );
    // InVersion: the file's (or stream's) format version, kMinVersion..kVersion.
    bool Decode( const uint8_t* InData, size_t InSize, std::vector<NetReplayTick>& OutTicks, std::string* OutError = nullptr,
        uint32_t InVersion = NetReplayFormat::kVersion );

    struct Info
    {
        uint32_t FirstTick = 0;
        uint32_t LastTick = 0;
        uint32_t TickCount = 0;
        NetReplayCodec Codec = NetReplayCodec::Raw;
        uint32_t RawBytes = 0;
        uint32_t StoredBytes = 0;
        uint64_t Checksum = 0;
    };
    // The fixed-size header at InData (kChunkHeaderBytes); false when it isn't one.
    bool ReadInfo( const uint8_t* InData, size_t InSize, Info& OutInfo );
}

namespace NetReplayFile
{
    // The file's opening: magic, version and the header.
    std::vector<uint8_t> EncodeHeader( const NetReplayHeader& InHeader );
    // Reads it from InData; OutSize: the bytes it took. False on a bad magic, a version outside
    // kMinVersion..kVersion, or a bad header.
    bool DecodeHeader( const uint8_t* InData, size_t InSize, NetReplayHeader& OutHeader, size_t& OutSize, std::string* OutError = nullptr );
}


// Where a replay's chunks come from: a file, or (ME-323) a network stream. Readers and playback
// take this, never a file directly.
class INetReplaySource
{
public:
    virtual ~INetReplaySource() = default;
    virtual const NetReplayHeader& GetHeader() const = 0;
    virtual size_t GetChunkCount() const = 0;
    virtual const NetReplayChunk::Info& GetChunkInfo( size_t InIndex ) const = 0;
    // The chunk's bytes as stored (header included), for NetReplayChunk::Decode.
    virtual bool ReadChunk( size_t InIndex, std::vector<uint8_t>& OutBytes ) = 0;
};


// A .mreplay file: the header and chunk table at Open, chunks read as asked.
class NetReplayFileSource final
    : public INetReplaySource
{
public:
    bool Open( const std::string& InPath );
    const std::string& GetError() const { return m_error; }
    // Whether the file ended with its index (false: the recorder never closed it; chunks were scanned).
    bool HasIndex() const { return m_hasIndex; }
    uint64_t GetFileSize() const { return m_fileSize; }

    const NetReplayHeader& GetHeader() const override { return m_header; }
    size_t GetChunkCount() const override { return m_chunks.size(); }
    const NetReplayChunk::Info& GetChunkInfo( size_t InIndex ) const override { return m_chunks[InIndex]; }
    bool ReadChunk( size_t InIndex, std::vector<uint8_t>& OutBytes ) override;

private:
    std::string m_path;
    std::string m_error;
    NetReplayHeader m_header;
    std::vector<NetReplayChunk::Info> m_chunks;
    std::vector<uint64_t> m_offsets;
    uint64_t m_fileSize = 0;
    bool m_hasIndex = false;
};


// Chunks held in memory (tests; a stream as it arrives).
class NetReplayMemorySource final
    : public INetReplaySource
{
public:
    explicit NetReplayMemorySource( NetReplayHeader InHeader ) : m_header( std::move( InHeader ) ) {}
    // False when the bytes aren't a chunk.
    bool AddChunk( std::vector<uint8_t> InBytes );

    const NetReplayHeader& GetHeader() const override { return m_header; }
    size_t GetChunkCount() const override { return m_chunks.size(); }
    const NetReplayChunk::Info& GetChunkInfo( size_t InIndex ) const override { return m_infos[InIndex]; }
    bool ReadChunk( size_t InIndex, std::vector<uint8_t>& OutBytes ) override;

private:
    NetReplayHeader m_header;
    std::vector<std::vector<uint8_t>> m_chunks;
    std::vector<NetReplayChunk::Info> m_infos;
};


// Ticks out of a source: any tick by number (it decodes that tick's chunk, from its keyframe, and
// keeps it), or a range in order.
class NetReplayReader
{
public:
    explicit NetReplayReader( INetReplaySource& InSource ) : m_source( InSource ) {}

    const NetReplayHeader& GetHeader() const { return m_source.GetHeader(); }
    bool IsEmpty() const { return m_source.GetChunkCount() == 0; }
    uint32_t GetFirstTick() const;
    uint32_t GetLastTick() const;
    uint64_t GetTickCount() const;

    // The record of InTick; false when the replay doesn't have it (or its chunk won't decode).
    bool ReadTick( uint32_t InTick, NetReplayTick& OutTick );
    // The first tick of the chunk holding InTick (its keyframe, which names every live NetId);
    // InTick when no chunk has it.
    uint32_t GetKeyframeTick( uint32_t InTick ) const;
    // InFn for every recorded tick in [InFrom, InTo], in order, until it returns false. False when
    // a chunk wouldn't decode.
    bool ForEach( uint32_t InFrom, uint32_t InTo, const std::function<bool( const NetReplayTick& )>& InFn );
    const std::string& GetError() const { return m_error; }

private:
    bool LoadChunk( size_t InIndex );

    INetReplaySource& m_source;
    size_t m_loaded = static_cast<size_t>( -1 );
    std::vector<NetReplayTick> m_ticks;
    std::vector<uint8_t> m_bytes;
    std::string m_error;
};


struct NetReplaySettings
{
    // Ticks per chunk: a keyframe and the deltas after it (5 s at 60 Hz). Seeking decodes at most this many.
    uint32_t KeyframeInterval = 300;
    NetReplayCodec Codec = NetReplayCodec::Deflate;
    // The in-memory ring of the newest ticks (ME-323's killcam reads it). 0: none.
    double RingSeconds = 12.0;
    // How often the open chunk goes to the file as a provisional "tail" chunk (overwritten by the
    // next one, or by the whole chunk): a server that dies loses about this much, not a whole
    // chunk. 0: only whole chunks (a crash loses up to KeyframeInterval ticks).
    double FlushSeconds = 1.0;
    // Chunks waiting for the disk past this much of the match: the file stops (failed), the ring
    // goes on, rather than holding them without end. 0: no limit.
    double MaxBacklogSeconds = 60.0;
    // Close waits this long for the last writes before giving the file up (a hung disk); 0: forever.
    double CloseTimeoutSeconds = 30.0;
};


// The server side: Record each tick; every KeyframeInterval ticks the chunk goes to a SerialQueue on
// the background lane (ME-319) to encode, compress and write, so the tick never waits on the disk.
class NetReplayRecorder
{
public:
    NetReplayRecorder( NetReplayHeader InHeader, const NetReplaySettings& InSettings = NetReplaySettings() );
    ~NetReplayRecorder();
    NetReplayRecorder( const NetReplayRecorder& ) = delete;
    NetReplayRecorder& operator=( const NetReplayRecorder& ) = delete;

    // Writes the file (its header now, on the calling thread); without it only the ring records.
    bool OpenFile( const std::string& InPath );
    // The tick's record (its Entities: the NetIds new or re-owned this tick; a chunk's first tick
    // gets every live one added here).
    void Record( NetReplayTick&& InTick );
    // The last chunk and the index, then waits for every write (up to CloseTimeoutSeconds). Recording ends.
    void Close();
    // Waits for the writes handed to the lane so far (tests; a tail is on disk after it).
    void WaitForWrites();

    const NetReplayHeader& GetHeader() const { return m_header; }
    const NetReplaySettings& GetSettings() const { return m_settings; }
    const std::string& GetPath() const { return m_path; }
    bool IsClosed() const { return m_closed; }
    // The newest ticks, oldest first (RingSeconds of them).
    const std::deque<std::shared_ptr<const NetReplayTick>>& GetRing() const { return m_ring; }
    // What every live NetId is, as of the newest tick (the ring's ticks carry only what changed,
    // except each KeyframeInterval-th: a killcam encoder starts its table from this).
    const std::map<uint32_t, NetReplayEntity>& GetEntities() const { return m_entities; }
    uint64_t GetTicksRecorded() const { return m_ticks; }
    // Written so far (the lane's writes that finished).
    uint64_t GetChunksWritten() const;
    uint64_t GetBytesWritten() const;
    // Chunk bytes before compression, for the ones written.
    uint64_t GetRawBytes() const;
    // Chunks handed to the lane and not written yet.
    uint32_t GetPendingWrites() const;
    // A write failed (the disk filled, the folder went): recording stops writing; the ring goes on.
    bool HasFailed() const;
    std::string GetError() const;

private:
    struct Writer;
    void SubmitChunk();
    void SubmitTail();

    NetReplayHeader m_header;
    NetReplaySettings m_settings;
    std::string m_path;
    std::shared_ptr<Writer> m_writer;
    std::unique_ptr<Jobs::SerialQueue> m_queue;
    std::vector<std::shared_ptr<const NetReplayTick>> m_chunk;
    std::deque<std::shared_ptr<const NetReplayTick>> m_ring;
    // What every live NetId is (a chunk's first tick lists them all).
    std::map<uint32_t, NetReplayEntity> m_entities;
    uint64_t m_ticks = 0;
    uint32_t m_chunkTicks = 0;      // ticks since the last keyframe (the next chunk's start at 0)
    uint32_t m_flushTicks = 0;      // FlushSeconds in ticks (0: no tails)
    bool m_closed = false;
    bool m_warnedBacklog = false;
};
