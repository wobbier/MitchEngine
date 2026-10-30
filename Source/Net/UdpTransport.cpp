#include "PCH.h"
#include "Net/UdpTransport.h"
#include <cstring>

#if USING( ME_PLATFORM_WINDOWS )
// PCH.h brings in winsock2.h / ws2tcpip.h ahead of Windows.h.
#pragma comment( lib, "Ws2_32.lib" )
#ifndef SIO_UDP_CONNRESET
#define SIO_UDP_CONNRESET _WSAIOW( IOC_VENDOR, 12 )
#endif
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace
{
#if USING( ME_PLATFORM_WINDOWS )
    using NativeSocket = SOCKET;
    const NativeSocket kInvalidSocket = INVALID_SOCKET;
    using AddressLength = int;

    int LastError()
    {
        return WSAGetLastError();
    }

    bool WouldBlock( int InError )
    {
        return InError == WSAEWOULDBLOCK;
    }

    bool Interrupted( int InError )
    {
        return InError == WSAEINTR;
    }

    // Winsock is reference counted: every transport (and every Resolve) holds it up.
    bool StartSockets()
    {
        WSADATA data;
        return WSAStartup( MAKEWORD( 2, 2 ), &data ) == 0;
    }

    void StopSockets()
    {
        WSACleanup();
    }

    void CloseSocket( NativeSocket InSocket )
    {
        closesocket( InSocket );
    }
#else
    using NativeSocket = int;
    const NativeSocket kInvalidSocket = -1;
    using AddressLength = socklen_t;

    int LastError()
    {
        return errno;
    }

    bool WouldBlock( int InError )
    {
        return InError == EAGAIN || InError == EWOULDBLOCK;
    }

    bool Interrupted( int InError )
    {
        return InError == EINTR;
    }

    bool StartSockets()
    {
        return true;
    }

    void StopSockets()
    {
    }

    void CloseSocket( NativeSocket InSocket )
    {
        close( InSocket );
    }
#endif

    NativeSocket ToNative( uint64_t InSocket )
    {
        return static_cast<NativeSocket>( InSocket );
    }

    sockaddr_in ToSockaddr( NetAddress InAddress )
    {
        sockaddr_in address;
        std::memset( &address, 0, sizeof( address ) );
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl( static_cast<uint32_t>( ( InAddress >> 16 ) & 0xFFFFFFFFu ) );
        address.sin_port = htons( static_cast<uint16_t>( InAddress & 0xFFFF ) );
        return address;
    }

    NetAddress FromSockaddr( const sockaddr_in& InAddress )
    {
        return UdpTransport::MakeAddress( ntohl( InAddress.sin_addr.s_addr ), ntohs( InAddress.sin_port ) );
    }

    // A dotted IPv4 or a host name (IPv4 only, so "localhost" can't come back as ::1); 0 when none.
    bool ResolveHost( const std::string& InHost, uint32_t& OutIPv4 )
    {
        in_addr parsed;
        if( inet_pton( AF_INET, InHost.c_str(), &parsed ) == 1 )
        {
            OutIPv4 = ntohl( parsed.s_addr );
            return true;
        }
        addrinfo hints;
        std::memset( &hints, 0, sizeof( hints ) );
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_DGRAM;
        addrinfo* results = nullptr;
        if( getaddrinfo( InHost.c_str(), nullptr, &hints, &results ) != 0 || results == nullptr )
        {
            return false;
        }
        OutIPv4 = ntohl( reinterpret_cast<const sockaddr_in*>( results->ai_addr )->sin_addr.s_addr );
        freeaddrinfo( results );
        return true;
    }

    std::string ErrorText( const char* InWhat, int InError )
    {
        return std::string( InWhat ) + " failed (error " + std::to_string( InError ) + ")";
    }
}


UdpTransport::UdpTransport( uint16_t InPort, const std::string& InBindAddress )
{
    if( !StartSockets() )
    {
        m_error = ErrorText( "WSAStartup", LastError() );
        return;
    }
    m_open = true;  // owns a WSAStartup from here: Close undoes it

    uint32_t bindIPv4 = 0;  // INADDR_ANY
    if( !InBindAddress.empty() && !ResolveHost( InBindAddress, bindIPv4 ) )
    {
        m_error = "can't resolve the bind address " + InBindAddress;
        Close();
        return;
    }

    const NativeSocket native = socket( AF_INET, SOCK_DGRAM, IPPROTO_UDP );
    if( native == kInvalidSocket )
    {
        m_error = ErrorText( "socket", LastError() );
        Close();
        return;
    }
    m_socket = static_cast<uint64_t>( native );
    m_hasSocket = true;

#if USING( ME_PLATFORM_WINDOWS )
    u_long nonBlocking = 1;
    const bool blockingOff = ioctlsocket( native, FIONBIO, &nonBlocking ) == 0;
    // An ICMP "port unreachable" (a peer not up yet) would otherwise fail the next recvfrom.
    BOOL reportResets = FALSE;
    DWORD returned = 0;
    WSAIoctl( native, SIO_UDP_CONNRESET, &reportResets, sizeof( reportResets ), nullptr, 0, &returned, nullptr, nullptr );
#else
    const int flags = fcntl( native, F_GETFL, 0 );
    const bool blockingOff = flags >= 0 && fcntl( native, F_SETFL, flags | O_NONBLOCK ) == 0;
#endif
    if( !blockingOff )
    {
        m_error = ErrorText( "non-blocking mode", LastError() );
        Close();
        return;
    }

    // Room for a server's burst of client packets between frames.
    const int bufferBytes = 1 << 20;
    setsockopt( native, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>( &bufferBytes ), sizeof( bufferBytes ) );
    setsockopt( native, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<const char*>( &bufferBytes ), sizeof( bufferBytes ) );

    // No SO_REUSEADDR: a second process on the same port should fail, not steal datagrams.
    sockaddr_in local = ToSockaddr( MakeAddress( bindIPv4, InPort ) );
    if( bind( native, reinterpret_cast<const sockaddr*>( &local ), sizeof( local ) ) != 0 )
    {
        m_error = ErrorText( ( "bind to port " + std::to_string( InPort ) ).c_str(), LastError() );
        Close();
        return;
    }
    AddressLength length = sizeof( local );
    if( getsockname( native, reinterpret_cast<sockaddr*>( &local ), &length ) == 0 )
    {
        m_address = FromSockaddr( local );
    }
}


UdpTransport::~UdpTransport()
{
    Close();
}


void UdpTransport::Close()
{
    if( m_hasSocket )
    {
        CloseSocket( ToNative( m_socket ) );
        m_hasSocket = false;
    }
    if( m_open )
    {
        StopSockets();
        m_open = false;
    }
}


void UdpTransport::Send( NetAddress InTo, const uint8_t* InData, size_t InSize )
{
    if( !m_hasSocket || InSize > kMaxDatagram )
    {
        ++m_sendErrors;
        return;
    }
    const sockaddr_in to = ToSockaddr( InTo );
    for( int attempt = 0; attempt < 2; ++attempt )
    {
        const auto sent = sendto( ToNative( m_socket ), reinterpret_cast<const char*>( InData ), static_cast<int>( InSize ), 0, reinterpret_cast<const sockaddr*>( &to ), sizeof( to ) );
        if( sent >= 0 )
        {
            return;
        }
        if( !Interrupted( LastError() ) )
        {
            break;
        }
    }
    // A full send buffer or an unreachable peer: the packet is lost, which NetConnection handles.
    ++m_sendErrors;
}


bool UdpTransport::Receive( NetPacket& OutPacket )
{
    if( !m_hasSocket )
    {
        return false;
    }
    for( ;; )
    {
        sockaddr_in from;
        AddressLength length = sizeof( from );
        const auto received = recvfrom( ToNative( m_socket ), reinterpret_cast<char*>( m_buffer ), static_cast<int>( sizeof( m_buffer ) ), 0, reinterpret_cast<sockaddr*>( &from ), &length );
        if( received < 0 )
        {
            const int error = LastError();
            if( WouldBlock( error ) )
            {
                return false;
            }
#if USING( ME_PLATFORM_WINDOWS )
            // A truncated datagram, or a reset some older send provoked: skip it and keep reading.
            if( error == WSAEMSGSIZE )
            {
                ++m_oversized;
                continue;
            }
            if( error == WSAECONNRESET )
            {
                continue;
            }
#endif
            if( Interrupted( error ) )
            {
                continue;
            }
            return false;
        }
        // Bigger than any packet NetConnection sends: not ours (and truncated anyway).
        if( static_cast<size_t>( received ) > kMaxDatagram )
        {
            ++m_oversized;
            continue;
        }
        OutPacket.From = FromSockaddr( from );
        OutPacket.Data.assign( m_buffer, m_buffer + received );
        return true;
    }
}


NetAddress UdpTransport::Resolve( const std::string& InHostAndPort )
{
    const size_t colon = InHostAndPort.rfind( ':' );
    if( colon == std::string::npos || colon == 0 || colon + 1 >= InHostAndPort.size() )
    {
        return 0;
    }
    const std::string host = InHostAndPort.substr( 0, colon );
    const std::string portText = InHostAndPort.substr( colon + 1 );
    if( portText.find_first_not_of( "0123456789" ) != std::string::npos || portText.size() > 5 )
    {
        return 0;
    }
    const unsigned long port = std::stoul( portText );
    if( port == 0 || port > 65535 )
    {
        return 0;
    }
    if( !StartSockets() )
    {
        return 0;
    }
    uint32_t ipv4 = 0;
    const bool resolved = ResolveHost( host, ipv4 );
    StopSockets();
    return resolved ? MakeAddress( ipv4, static_cast<uint16_t>( port ) ) : 0;
}


std::string UdpTransport::ToString( NetAddress InAddress )
{
    const uint32_t ipv4 = static_cast<uint32_t>( ( InAddress >> 16 ) & 0xFFFFFFFFu );
    return std::to_string( ( ipv4 >> 24 ) & 0xFF ) + "." + std::to_string( ( ipv4 >> 16 ) & 0xFF ) + "." + std::to_string( ( ipv4 >> 8 ) & 0xFF ) + "."
        + std::to_string( ipv4 & 0xFF ) + ":" + std::to_string( InAddress & 0xFFFF );
}
