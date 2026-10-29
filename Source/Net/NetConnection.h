#pragma once
#include "Net/NetTransport.h"
#include <array>
#include <cstdint>
#include <deque>
#include <unordered_map>
#include <vector>

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
};


// One end of a link to a remote address over a transport (NET-1). Every SendPacket sends one
// packet carrying acks for what this end received, the reliable messages that are new or due a
// resend, and the unreliable messages queued since the last packet. The reliable stream arrives
// exactly once and in order whatever the link does; unreliable messages arrive at most once.
//
// Header: protocol id, sequence, the latest sequence received plus a 32-bit field of the ones
// before it (each packet acks 33), and how long that latest packet waited before this ack (so
// the round trip excludes the remote's send rate). Sequences and message ids wrap at 16 bits.
class NetConnection
{
public:
    static constexpr uint16_t kProtocolId = 0x4D45;
    static constexpr size_t kMaxPacketBytes = 1200;
    static constexpr size_t kMaxMessageBytes = 1024;
    // Reliable messages in flight at once; later ones wait their turn.
    static constexpr uint16_t kReliableWindow = 1024;

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

    bool ReceiveReliable( std::vector<uint8_t>& OutMessage );
    bool ReceiveUnreliable( std::vector<uint8_t>& OutMessage );

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
    struct SentPacket
    {
        uint16_t Sequence = 0;
        bool Valid = false;
        bool Acked = false;
        double SentAt = 0.0;
        std::vector<uint16_t> Reliable;
    };

    double ResendDelay() const;
    void ProcessAcks( uint16_t InAck, uint32_t InAckBits, double InAckDelay, double InNow );
    void OnAcked( SentPacket& InPacket );
    bool RecordReceived( uint16_t InSequence, double InNow );
    void AcceptReliable( uint16_t InId, std::vector<uint8_t>&& InData );

    INetTransport& m_transport;
    NetAddress m_remote;
    NetStats m_stats;

    // Sending.
    uint16_t m_sequence = 0;
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
    uint32_t m_receivedBits = 0;
    double m_remoteSequenceAt = 0.0;
    uint16_t m_nextDeliverId = 0;
    std::unordered_map<uint16_t, std::vector<uint8_t>> m_early;
    std::deque<std::vector<uint8_t>> m_reliableIn;
    std::deque<std::vector<uint8_t>> m_unreliableIn;
};

// Wrap-aware sequence order: InA after InB (within half the 16-bit range).
inline bool NetSequenceGreater( uint16_t InA, uint16_t InB )
{
    return InA != InB && static_cast<uint16_t>( InA - InB ) < 0x8000;
}
