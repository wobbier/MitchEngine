#pragma once
#include "Net/NetTransport.h"
#include <cstdint>
#include <string>

// UDP over IPv4 (NET-5): a plain socket datagram transport for LAN and dedicated servers.
// NetConnection already does acks, ordering and the reliable stream, so this only moves packets.
// Addresses pack the IPv4 address and port as (ip << 16) | port, host byte order: the address a
// client connects to is exactly the From its replies carry (IPv6 doesn't fit in a NetAddress).
// Non-blocking: Receive returns false once nothing more has arrived.
class UdpTransport final
    : public INetTransport
{
public:
    // Binds InPort on InBindAddress ("" = every interface; 0 = any free port). Check IsOpen.
    explicit UdpTransport( uint16_t InPort = 0, const std::string& InBindAddress = "" );
    ~UdpTransport() override;

    UdpTransport( const UdpTransport& ) = delete;
    UdpTransport& operator=( const UdpTransport& ) = delete;

    bool IsOpen() const { return m_open; }
    // Why the socket couldn't open ("" when it did).
    const std::string& GetError() const { return m_error; }
    // The bound address; the IPv4 part is 0.0.0.0 when bound to every interface.
    NetAddress GetAddress() const override { return m_address; }
    uint16_t GetPort() const { return static_cast<uint16_t>( m_address & 0xFFFF ); }

    void Send( NetAddress InTo, const uint8_t* InData, size_t InSize ) override;
    bool Receive( NetPacket& OutPacket ) override;

    // Datagrams the OS refused to send (full buffers, unreachable), and ones that arrived too big.
    uint64_t GetSendErrors() const { return m_sendErrors; }
    uint64_t GetOversized() const { return m_oversized; }

    static NetAddress MakeAddress( uint32_t InIPv4, uint16_t InPort ) { return ( static_cast<NetAddress>( InIPv4 ) << 16 ) | InPort; }
    // "host:port" (a name or dotted IPv4; "localhost" works) to an address; 0 when it doesn't resolve.
    static NetAddress Resolve( const std::string& InHostAndPort );
    // "a.b.c.d:port".
    static std::string ToString( NetAddress InAddress );

    // The largest datagram Receive accepts (NetConnection packets are at most 1200 bytes).
    static constexpr size_t kMaxDatagram = 1500;

private:
    void Close();

    uint64_t m_socket = 0;
    bool m_hasSocket = false;
    bool m_open = false;
    NetAddress m_address = 0;
    std::string m_error;
    uint64_t m_sendErrors = 0;
    uint64_t m_oversized = 0;
    uint8_t m_buffer[kMaxDatagram + 1] = {};
};
