#pragma once
#include <cstddef>
#include <cstdint>
#include <deque>
#include <unordered_map>
#include <vector>

// Datagram transports (NET-1): unreliable, unordered packets between addresses. NetConnection
// builds acks, the reliable stream and RTT on top. Real sockets (Steam, UDP) come later; the
// loopback joins worlds in one process (tests, listen servers), and the conditioned wrapper adds
// latency, jitter, loss, duplicates and reordering, deterministically from a seed.
using NetAddress = uint64_t;

struct NetPacket
{
    NetAddress From = 0;
    std::vector<uint8_t> Data;
};


class INetTransport
{
public:
    virtual ~INetTransport() = default;

    virtual NetAddress GetAddress() const = 0;
    virtual void Send( NetAddress InTo, const uint8_t* InData, size_t InSize ) = 0;
    // The next packet that has arrived, if any.
    virtual bool Receive( NetPacket& OutPacket ) = 0;
    // Time-driven transports deliver what's due (seconds, any monotonic origin); sockets ignore it.
    virtual void Update( double InNow ) {}
};


class LoopbackTransport;

// The in-process "network" loopback transports deliver through.
class LoopbackNetwork
{
public:
    void Register( LoopbackTransport& InTransport );
    void Unregister( LoopbackTransport& InTransport );
    // False when nothing has that address (the packet is gone, as on a real network).
    bool Deliver( NetAddress InFrom, NetAddress InTo, const uint8_t* InData, size_t InSize );

private:
    std::unordered_map<NetAddress, LoopbackTransport*> m_transports;
};


class LoopbackTransport final
    : public INetTransport
{
public:
    LoopbackTransport( LoopbackNetwork& InNetwork, NetAddress InAddress );
    ~LoopbackTransport() override;

    NetAddress GetAddress() const override { return m_address; }
    void Send( NetAddress InTo, const uint8_t* InData, size_t InSize ) override;
    bool Receive( NetPacket& OutPacket ) override;

private:
    friend class LoopbackNetwork;
    LoopbackNetwork& m_network;
    NetAddress m_address;
    std::deque<NetPacket> m_inbox;
};


// What a conditioned link does to outgoing packets.
struct NetConditions
{
    float LatencyMs = 0.f;          // one way
    float JitterMs = 0.f;           // +/- uniformly around the latency
    float Loss = 0.f;               // 0-1: dropped
    float Duplicate = 0.f;          // 0-1: sent twice (the copy with its own delay)
    float Reorder = 0.f;            // 0-1: held back an extra ReorderMs, so later packets pass it
    float ReorderMs = 30.f;
    uint64_t Seed = 1;
};


// Wraps a transport and applies NetConditions to what it sends (wrap both ends for a two-way
// link). Sends are held until due and released by Update, so drive Update every tick.
class ConditionedTransport final
    : public INetTransport
{
public:
    ConditionedTransport( INetTransport& InInner, const NetConditions& InConditions );

    void SetConditions( const NetConditions& InConditions );
    const NetConditions& GetConditions() const { return m_conditions; }

    NetAddress GetAddress() const override { return m_inner.GetAddress(); }
    void Send( NetAddress InTo, const uint8_t* InData, size_t InSize ) override;
    bool Receive( NetPacket& OutPacket ) override { return m_inner.Receive( OutPacket ); }
    void Update( double InNow ) override;

    size_t GetDroppedCount() const { return m_dropped; }
    size_t GetDuplicatedCount() const { return m_duplicated; }

private:
    struct Held
    {
        double Due = 0.0;
        uint64_t Order = 0;
        NetAddress To = 0;
        std::vector<uint8_t> Data;
    };

    double NextUnit();
    void Hold( NetAddress InTo, const uint8_t* InData, size_t InSize );

    INetTransport& m_inner;
    NetConditions m_conditions;
    uint64_t m_rng = 1;
    double m_now = 0.0;
    uint64_t m_order = 0;
    std::vector<Held> m_held;
    size_t m_dropped = 0;
    size_t m_duplicated = 0;
};
