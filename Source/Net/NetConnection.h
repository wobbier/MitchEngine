#pragma once
#include "Net/NetTransport.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <deque>
#include <set>
#include <unordered_map>
#include <vector>

class BitWriter;

// What a connection has seen (NET-1).
struct NetStats
{
    uint64_t PacketsSent = 0;
    uint64_t PacketsReceived = 0;
    uint64_t PacketsAcked = 0;
    uint64_t PacketsLost = 0;           // not acked within the 33-packet ack window
    uint64_t PacketsRejected = 0;       // wrong protocol, malformed
    uint64_t DuplicatesDropped = 0;     // a packet sequence seen before
    uint64_t BytesSent = 0;
    uint64_t BytesReceived = 0;
    uint64_t ReliableSent = 0;          // messages queued
    uint64_t ReliableResent = 0;        // ... and sent again after no ack
    uint64_t ReliableDelivered = 0;     // received, in order
    float RoundTripMs = 0.f;            // smoothed; 0 until the first ack
    float RoundTripDeviationMs = 0.f;
    float PacketLoss = 0.f;             // smoothed fraction of packets lost
    // The blob channel (ME-323).
    uint64_t BlobsSent = 0;             // queued (SendBlob)
    uint64_t BlobsAcked = 0;            // ... and every slice acked: the remote has it all (delivered,
                                        // unless it refused it while the last slices were on the way)
    uint64_t BlobsReceived = 0;         // reassembled and delivered
    uint64_t BlobsCancelled = 0;        // by this end, either way (CancelBlob, CancelIncomingBlob)
    uint64_t BlobsCancelledByRemote = 0;
    uint64_t BlobsRefused = 0;          // empty or past kMaxBlobBytes
    uint64_t BlobSlicesSent = 0;        // resends too
    uint64_t BlobSlicesResent = 0;
    uint64_t BlobSlicesDuplicate = 0;   // received again (a resend crossed its ack)
    uint64_t BlobSlicesIgnored = 0;     // received and dropped: incoming blobs not allowed here yet, or
                                        // not an honest peer's (an id too far ahead, a ninth blob at once,
                                        // a size that changed)
    uint64_t BlobBytesSent = 0;         // slice payload, resends too
    uint64_t BlobBytesReceived = 0;     // new slice payload
    uint64_t BlobPacketsSent = 0;       // packets of slices alone, past the one SendPacket always sends
};


// How much of a link the blob channel may use (ME-323). Live traffic (the packet every SendPacket
// sends: snapshots, owner state, commands, reliable messages) always goes first and is never held
// back; blobs get what it leaves of LinkBytesPerSecond, and at least MinBytesPerSecond whatever it
// takes, so a blob always moves. Both are token buckets refilled at each SendPacket.
struct NetBlobSettings
{
    uint32_t LinkBytesPerSecond = 128 * 1024;
    uint32_t MinBytesPerSecond = 16 * 1024;
};


// A blob on its way, either way (NetConnection::GetBlobProgress).
struct NetBlobProgress
{
    uint32_t Id = 0;
    bool Outgoing = true;
    bool Started = true;            // outgoing: has one of the kMaxActiveBlobs slots (else queued)
    size_t Size = 0;
    size_t DoneBytes = 0;           // outgoing: acked; incoming: received
    size_t ContiguousBytes = 0;     // incoming: from the start without a gap
    double StartedAt = -1.0;        // its first slice sent, or received (the InNow clock); < 0: not yet
};


// One end of a link to a remote address over a transport (NET-1). Every SendPacket sends one
// packet carrying acks for what this end received, the reliable messages that are new or due a
// resend, and the unreliable messages queued since the last packet. The reliable stream arrives
// exactly once and in order whatever the link does; unreliable messages arrive at most once.
//
// Header: protocol id, sequence, the latest sequence received plus a 32-bit field of the ones
// before it (each packet acks 33), and how long that latest packet waited before this ack (so
// the round trip excludes the remote's send rate). Sequences and message ids wrap at 16 bits.
//
// Blobs (ME-323): messages up to kMaxBlobBytes, sent in kBlobSliceBytes slices that are resent
// until acked and reassembled on the far side, which delivers each blob whole once it has every
// slice (blobs complete in any order; a blob's bytes are always in order). They ride after the
// messages, in a trailer older peers skip: in the room left in the packet SendPacket always sends,
// then in packets of their own, within the NetBlobSettings share. Either end can cancel one.
class NetConnection
{
public:
    static constexpr uint16_t kProtocolId = 0x4D45;
    static constexpr size_t kMaxPacketBytes = 1200;
    static constexpr size_t kMaxMessageBytes = 1024;
    // Reliable messages in flight at once; later ones wait their turn.
    static constexpr uint16_t kReliableWindow = 1024;
    // Blobs: the largest, the slice, and how many go at once (later ones queue).
    static constexpr size_t kMaxBlobBytes = 4 * 1024 * 1024;
    static constexpr size_t kBlobSliceBytes = 1024;
    static constexpr size_t kMaxActiveBlobs = 4;
    // Incoming blobs a receiver holds at once (an honest sender never has more than kMaxActiveBlobs).
    static constexpr size_t kMaxIncomingBlobs = 8;
    // Packets of slices alone one SendPacket may add.
    static constexpr int kMaxBlobPacketsPerSend = 16;
    // Ids the remote cancelled before this end saw a slice of them, held until the oldest blob not
    // done gets past them, at most (past it, such cancels are ignored: a hostile peer's).
    static constexpr size_t kMaxIncomingFinished = 4096;

    NetConnection( INetTransport& InTransport, NetAddress InRemote );

    NetAddress GetRemote() const { return m_remote; }

    // Messages up to kMaxMessageBytes; false (and nothing queued) when larger.
    bool SendReliable( const void* InData, size_t InSize );
    bool SendUnreliable( const void* InData, size_t InSize );
    bool SendReliable( const std::vector<uint8_t>& InData ) { return SendReliable( InData.data(), InData.size() ); }
    bool SendUnreliable( const std::vector<uint8_t>& InData ) { return SendUnreliable( InData.data(), InData.size() ); }

    // Builds and sends one packet (InNow: seconds, the clock ReceivePacket gets).
    void SendPacket( double InNow );
    // A packet from the remote. False when it was rejected or a duplicate.
    bool ReceivePacket( const uint8_t* InData, size_t InSize, double InNow );
    // A pass reading the remote's packets begins (every frame or tick, packets or not). Long after
    // the last one, the packets read now may have sat unread meanwhile (this end hitched inside a
    // frame, after its last read): their round trips aren't the link's. Long: over two usual
    // gaps between passes. Optional: an end that never calls it judges by its last send alone.
    void BeginRead( double InNow );

    bool ReceiveReliable( std::vector<uint8_t>& OutMessage );
    bool ReceiveUnreliable( std::vector<uint8_t>& OutMessage );

    // Blobs (ME-323). SendBlob queues one: its id (never 0), or 0 when it's empty or past
    // kMaxBlobBytes (refused: nothing queued). CancelBlob stops one of this end's that isn't known
    // to be done yet (false otherwise: every slice acked, or cancelled already); the remote drops
    // what it has of it. A cancel can lose the race: the remote may already have the blob whole,
    // its last acks still on their way (then it's delivered there all the same).
    // CancelIncomingBlob drops one arriving from the remote and tells it to stop sending (slices
    // already on their way are acked and dropped: the sender may count it acked first).
    uint32_t SendBlob( const void* InData, size_t InSize );
    uint32_t SendBlob( const std::vector<uint8_t>& InData ) { return SendBlob( InData.data(), InData.size() ); }
    bool CancelBlob( uint32_t InId );
    bool CancelIncomingBlob( uint32_t InId );
    // A blob the remote sent, whole (OutId: the remote's id for it).
    bool ReceiveBlob( uint32_t& OutId, std::vector<uint8_t>& OutData );
    // Every blob on its way: this end's (queued too) first, then the remote's.
    std::vector<NetBlobProgress> GetBlobProgress() const;
    size_t GetPendingBlobCount() const { return m_blobsOut.size(); }
    void SetBlobSettings( const NetBlobSettings& InSettings ) { m_blobSettings = InSettings; }
    // Whether the remote's blobs are taken in (default yes). Off, their slices and cancels are
    // dropped before anything is reserved for them (BlobSlicesIgnored): a server turns it on only
    // once it accepted the peer, so an address that never said Hello can't make it hold megabytes.
    void SetAcceptIncomingBlobs( bool InAccept ) { m_acceptIncomingBlobs = InAccept; }
    bool IsAcceptingIncomingBlobs() const { return m_acceptIncomingBlobs; }
    const NetBlobSettings& GetBlobSettings() const { return m_blobSettings; }
    // About the packet ReceivePacket last accepted (drained right after that call, the messages
    // above are its). Newest: no packet the remote sent later had arrived before it (it moved the
    // received sequence on; a reordered older one didn't). Wrap-safe however long the link idles,
    // since every packet, keep-alives too, moves the sequence on. Packets of blob slices alone
    // don't count: only those SendPacket always sends do (a slice packet sent right after one and
    // arriving first mustn't make it old news).
    bool WasLastPacketNewest() const { return m_lastPacketNewest; }
    // The newest of this end's packets the remote had received when it sent that packet (false
    // when it had none yet): what the remote knew of this end when it wrote the messages. Only the
    // packets SendPacket always sends count (not packets of blob slices alone, which carry no
    // messages): an ack at or past one of them means the remote has it or a later one like it.
    bool GetLastPacketAck( uint16_t& OutAck ) const
    {
        OutAck = m_lastPacketAck;
        return m_lastPacketHasAck;
    }
    // The sequence the next SendPacket's packet will carry.
    uint16_t GetNextSequence() const { return m_sequence; }

    // Reliable messages not yet acked.
    size_t GetPendingReliableCount() const { return m_outgoing.size(); }
    const NetStats& GetStats() const { return m_stats; }

private:
    struct OutgoingReliable
    {
        uint16_t Id = 0;
        std::vector<uint8_t> Data;
        double LastSent = -1.0;     // < 0: never sent
    };
    struct BlobSliceRef
    {
        uint32_t Blob = 0;
        uint32_t Index = 0;
    };
    struct BlobCancel
    {
        uint32_t Id = 0;
        bool Theirs = false;        // about the remote's blob (this end refused it), else this end's
        double LastSent = -1.0;
    };
    struct SentPacket
    {
        uint16_t Sequence = 0;
        bool Valid = false;
        bool Acked = false;
        bool SlicesOnly = false;    // a packet of blob slices alone (SendBlobPacket)
        double SentAt = 0.0;
        std::vector<uint16_t> Reliable;
        std::vector<BlobSliceRef> Slices;
        std::vector<BlobCancel> Cancels;
    };
    struct OutgoingBlob
    {
        uint32_t Id = 0;
        std::vector<uint8_t> Data;
        uint32_t SliceCount = 0;
        std::vector<double> SliceSentAt;    // < 0: never sent
        std::vector<bool> SliceAcked;
        uint32_t AckedCount = 0;
        uint32_t NextNew = 0;
        std::deque<uint32_t> InFlight;      // slices sent and not known acked, oldest send first
        double StartedAt = -1.0;
    };
    struct IncomingBlob
    {
        uint32_t Id = 0;
        std::vector<uint8_t> Data;
        std::vector<bool> Have;
        uint32_t HaveCount = 0;
        uint32_t Contiguous = 0;            // slices from the first without a gap
        double StartedAt = 0.0;
    };
    struct ReceivedSlice
    {
        uint32_t Blob = 0;
        uint32_t Size = 0;
        uint32_t Index = 0;
        std::vector<uint8_t> Data;
    };

    double ResendDelay() const;
    // A slice's: ResendDelay plus how long the remote holds a packet before its ack goes out (its
    // gap between packets: a 20 Hz end acks 50 ms late, which the round trip leaves out).
    double BlobResendDelay() const { return ResendDelay() + m_remoteAckDelay; }
    void WriteHeader( BitWriter& InWriter, uint16_t InSequence, double InNow ) const;
    void ProcessAcks( uint16_t InAck, uint32_t InAckBits, double InAckDelay, double InNow );
    void OnAcked( SentPacket& InPacket );
    bool RecordReceived( uint16_t InSequence, double InNow );
    void AcceptReliable( uint16_t InId, std::vector<uint8_t>&& InData );

    // Blobs.
    static uint32_t SliceCount( size_t InSize ) { return static_cast<uint32_t>( ( InSize + kBlobSliceBytes - 1 ) / kBlobSliceBytes ); }
    static size_t SliceBytes( size_t InSize, uint32_t InIndex ) { return std::min( kBlobSliceBytes, InSize - static_cast<size_t>( InIndex ) * kBlobSliceBytes ); }
    void RefillBlobTokens( double InNow );
    // How many seconds of their rate the blob buckets hold (and owe at most): a little over this
    // end's usual gap between packets, so a slow tick rate can still spend the whole rate.
    double BlobBurstSeconds() const;
    // The newest packet SendPacket always sent among InAck and the 32 before it the remote acked.
    bool FindMainAck( uint16_t InAck, uint32_t InAckBits, uint16_t& OutAck ) const;
    bool HasBlobAllowance() const { return m_blobLinkTokens > 0.0 || m_blobMinTokens > 0.0; }
    size_t GetActiveBlobSlots() const;
    bool HasSliceDue( double InNow, double InResendDelay ) const;
    // A packet of slices alone (after the one SendPacket always sends): false, and nothing sent,
    // when no slice fit the share.
    bool SendBlobPacket( double InNow );
    // The next slice due (a resend overdue, else a new one) that fits InRoom bytes of payload,
    // taking turns between the active blobs; marks it sent at InNow. False when none is.
    bool TakeSlice( double InNow, double InResendDelay, size_t InRoom, BlobSliceRef& OutSlice );
    // Writes the trailer (cancels due, unless InExtra, then slices while they fit InBudget and the
    // share allows): false, and nothing written or changed, when there was nothing to write.
    // InBudget shrinks by what was written.
    bool WriteBlobTrailer( BitWriter& InWriter, bool InExtra, size_t& InBudget, double InNow, SentPacket& InRecord );
    void OnSliceAcked( const BlobSliceRef& InSlice );
    void AcceptSlice( ReceivedSlice&& InSlice, double InNow );
    void AcceptBlobCancel( uint32_t InId, bool InTheirs );
    void MarkIncomingFinished( uint32_t InId );
    bool IsIncomingFinished( uint32_t InId ) const { return InId < m_blobInFloor || m_blobInFinished.count( InId ) > 0; }

    INetTransport& m_transport;
    NetAddress m_remote;
    NetStats m_stats;

    // Sending.
    uint16_t m_sequence = 0;
    double m_lastSentAt = -1.0;
    double m_sendGap = 0.0;             // smoothed seconds between packets sent
    double m_lastReadAt = -1.0;         // BeginRead's last pass
    double m_readGap = 0.0;             // ... the time since the one before it
    double m_readInterval = -1.0;       // ... and the usual time between them (smoothed; < 0: unknown)
    double m_remoteAckDelay = 0.0;      // the remote's wait before acking, about its longest lately
    std::array<SentPacket, 1024> m_sent;
    uint16_t m_finalized = 0xFFFF;      // sent packets up to here are counted acked or lost
    bool m_hasAck = false;
    uint16_t m_latestAck = 0;
    std::deque<OutgoingReliable> m_outgoing;
    uint16_t m_nextReliableId = 0;
    std::vector<std::vector<uint8_t>> m_unreliable;

    // Receiving.
    bool m_hasReceived = false;
    uint16_t m_remoteSequence = 0;
    bool m_lastPacketNewest = false;
    bool m_lastPacketHasAck = false;
    uint16_t m_lastPacketAck = 0;
    uint32_t m_receivedBits = 0;
    double m_remoteSequenceAt = 0.0;
    uint16_t m_nextDeliverId = 0;
    std::unordered_map<uint16_t, std::vector<uint8_t>> m_early;
    std::deque<std::vector<uint8_t>> m_reliableIn;
    std::deque<std::vector<uint8_t>> m_unreliableIn;
    // Main packets only (not packets of slices alone), for WasLastPacketNewest.
    bool m_hasMainReceived = false;
    uint16_t m_mainSequence = 0;

    // Blobs, sending.
    NetBlobSettings m_blobSettings;
    std::deque<OutgoingBlob> m_blobsOut;        // the first GetActiveBlobSlots() send; the rest queue
    uint32_t m_nextBlobId = 1;
    size_t m_blobTurn = 0;                      // round robin between active blobs
    std::vector<BlobCancel> m_blobCancels;      // until a packet carrying it is acked
    double m_blobLinkTokens = 0.0;
    double m_blobMinTokens = 0.0;
    double m_blobRefilledAt = -1.0;
    // Blobs, receiving: every id below the floor, and those in the set, are done (delivered or cancelled).
    bool m_acceptIncomingBlobs = true;
    std::vector<IncomingBlob> m_blobsIn;
    uint32_t m_blobInFloor = 1;
    std::set<uint32_t> m_blobInFinished;
    std::deque<std::pair<uint32_t, std::vector<uint8_t>>> m_blobsDelivered;
};

// Wrap-aware sequence order: InA after InB (within half the 16-bit range).
inline bool NetSequenceGreater( uint16_t InA, uint16_t InB )
{
    return InA != InB && static_cast<uint16_t>( InA - InB ) < 0x8000;
}
