#include "PCH.h"
#include "Net/NetSession.h"
#include "Net/BitStream.h"
#include "Net/NetQuantize.h"
#include <algorithm>
#include <cmath>

namespace
{
    // A rejected client keeps its connection this long, so the Reject gets through.
    constexpr double kRejectLinger = 2.0;
    constexpr double kHandshakeTimeout = 10.0;
    constexpr float kSlackRange = 64.f;
    constexpr int kSlackBits = 14;

    void Send( NetConnection& InConnection, const BitWriter& InWriter, bool InReliable )
    {
        if( InReliable )
        {
            InConnection.SendReliable( InWriter.GetBytes() );
        }
        else
        {
            InConnection.SendUnreliable( InWriter.GetBytes() );
        }
    }
}


NetServer::NetServer( INetTransport& InTransport, const NetVersion& InVersion, uint16_t InTickRate, size_t InMaxClients )
    : m_transport( InTransport )
    , m_version( InVersion )
    , m_tickRate( InTickRate )
    , m_maxClients( InMaxClients )
{
}


NetServer::Client* NetServer::FindOrAdd( NetAddress InAddress )
{
    for( std::unique_ptr<Client>& client : m_clients )
    {
        if( client->Address == InAddress )
        {
            return client.get();
        }
    }
    // Room for everyone accepted plus a few mid-handshake.
    if( m_clients.size() >= m_maxClients + 4 )
    {
        return nullptr;
    }
    auto client = std::make_unique<Client>();
    client->Address = InAddress;
    client->Connection = std::make_unique<NetConnection>( m_transport, InAddress );
    m_clients.push_back( std::move( client ) );
    return m_clients.back().get();
}


void NetServer::Receive( double InNow )
{
    NetPacket packet;
    while( m_transport.Receive( packet ) )
    {
        Client* client = FindOrAdd( packet.From );
        if( !client || !client->Connection->ReceivePacket( packet.Data.data(), packet.Data.size(), InNow ) )
        {
            continue;
        }
        client->LastHeard = InNow;
        std::vector<uint8_t> message;
        while( client->Connection->ReceiveReliable( message ) )
        {
            HandleReliable( *client, message );
        }
        while( client->Connection->ReceiveUnreliable( message ) )
        {
            HandleUnreliable( *client, message );
        }
    }
    // Rejected clients go once their Reject has been delivered (or they stop listening).
    m_clients.erase( std::remove_if( m_clients.begin(), m_clients.end(), [InNow]( const std::unique_ptr<Client>& InClient ) {
        if( InClient->Rejected )
        {
            return InClient->RejectedAt > 0.0 && ( InClient->Connection->GetPendingReliableCount() == 0 || InNow - InClient->RejectedAt > kRejectLinger );
        }
        // Someone who never finished saying hello.
        return !InClient->Accepted && InNow - InClient->LastHeard > kHandshakeTimeout;
    } ), m_clients.end() );
}


void NetServer::HandleReliable( Client& InClient, const std::vector<uint8_t>& InMessage )
{
    BitReader reader( InMessage );
    if( reader.ReadBits( 8 ) != NetMessage::Hello || InClient.Accepted || InClient.Rejected )
    {
        return;
    }
    const NetVersion theirs = NetVersion::Read( reader );
    std::string reason;
    if( reader.IsOverflowed() )
    {
        reason = "malformed hello";
    }
    else if( const std::vector<std::string> differences = m_version.Differences( theirs ); !differences.empty() )
    {
        reason = "version mismatch:";
        for( const std::string& difference : differences )
        {
            reason += " " + difference;
        }
    }
    else if( GetClientCount() >= m_maxClients )
    {
        reason = "server full";
    }

    BitWriter reply;
    if( !reason.empty() )
    {
        reply.WriteBits( NetMessage::Reject, 8 );
        reply.WriteString( reason );
        InClient.Rejected = true;
        m_lastRejection = reason;
    }
    else
    {
        InClient.Accepted = true;
        InClient.Id = m_nextId++;
        reply.WriteBits( NetMessage::Welcome, 8 );
        reply.WriteBits( InClient.Id, 8 );
        reply.WriteBits( m_tickRate, 16 );
        reply.WriteBits( m_tick, 32 );
    }
    Send( *InClient.Connection, reply, true );
}


void NetServer::HandleUnreliable( Client& InClient, const std::vector<uint8_t>& InMessage )
{
    BitReader reader( InMessage );
    if( !InClient.Accepted || reader.ReadBits( 8 ) != NetMessage::Commands )
    {
        return;
    }
    std::vector<InputCommand> commands;
    if( !NetCommands::Read( reader, commands ) )
    {
        return;
    }
    for( const InputCommand& command : commands )
    {
        InClient.Commands.Receive( command, m_tick );
    }
}


void NetServer::BeginTick()
{
    for( std::unique_ptr<Client>& client : m_clients )
    {
        if( client->Accepted )
        {
            client->Current = client->Commands.Consume( m_tick );
        }
    }
}


void NetServer::EndTick( double InNow )
{
    for( std::unique_ptr<Client>& client : m_clients )
    {
        if( client->Rejected && client->RejectedAt == 0.0 )
        {
            client->RejectedAt = InNow;
        }
        if( client->Accepted )
        {
            BitWriter state;
            state.WriteBits( NetMessage::ServerState, 8 );
            state.WriteBits( m_tick, 32 );
            state.WriteBool( client->Commands.HasReceived() );
            state.WriteBits( client->Commands.GetNewestTick(), 32 );
            NetQuantize::WriteFloat( state, client->Commands.GetSlack(), -kSlackRange, kSlackRange, kSlackBits );
            Send( *client->Connection, state, false );
        }
        client->Connection->SendPacket( InNow );
    }
    ++m_tick;
}


size_t NetServer::GetClientCount() const
{
    return static_cast<size_t>( std::count_if( m_clients.begin(), m_clients.end(), []( const std::unique_ptr<Client>& InClient ) { return InClient->Accepted; } ) );
}


const NetServer::Client* NetServer::FindClient( uint8_t InId ) const
{
    for( const std::unique_ptr<Client>& client : m_clients )
    {
        if( client->Accepted && client->Id == InId )
        {
            return client.get();
        }
    }
    return nullptr;
}


NetClient::NetClient( INetTransport& InTransport, NetAddress InServer, const NetVersion& InVersion, const NetClockSettings& InClock )
    : m_transport( InTransport )
    , m_connection( InTransport, InServer )
    , m_version( InVersion )
    , m_clock( InClock )
{
}


void NetClient::Connect()
{
    if( m_state != State::Disconnected )
    {
        return;
    }
    BitWriter hello;
    hello.WriteBits( NetMessage::Hello, 8 );
    m_version.Write( hello );
    m_connection.SendReliable( hello.GetBytes() );
    m_state = State::Connecting;
}


void NetClient::Receive( double InNow )
{
    NetPacket packet;
    while( m_transport.Receive( packet ) )
    {
        if( packet.From != m_connection.GetRemote() || !m_connection.ReceivePacket( packet.Data.data(), packet.Data.size(), InNow ) )
        {
            continue;
        }
        std::vector<uint8_t> message;
        while( m_connection.ReceiveReliable( message ) )
        {
            HandleReliable( message, InNow );
        }
        while( m_connection.ReceiveUnreliable( message ) )
        {
            HandleUnreliable( message );
        }
    }
}


void NetClient::HandleReliable( const std::vector<uint8_t>& InMessage, double InNow )
{
    BitReader reader( InMessage );
    const uint32_t type = reader.ReadBits( 8 );
    if( m_state != State::Connecting )
    {
        return;
    }
    if( type == NetMessage::Reject )
    {
        m_rejectReason = reader.ReadString( 256 );
        m_state = State::Rejected;
    }
    else if( type == NetMessage::Welcome )
    {
        m_clientId = static_cast<uint8_t>( reader.ReadBits( 8 ) );
        m_tickRate = static_cast<uint16_t>( reader.ReadBits( 16 ) );
        const uint32_t serverTick = reader.ReadBits( 32 );
        if( reader.IsOverflowed() || m_tickRate == 0 )
        {
            return;
        }
        // The server is half a round trip past that tick by now; run ahead of it by another half
        // (so commands arrive in time) plus the slack.
        const double rttTicks = m_connection.GetStats().RoundTripMs / 1000.0 * m_tickRate;
        m_clock.Start( serverTick + static_cast<uint32_t>( std::ceil( rttTicks + m_clock.GetSettings().TargetSlack ) ) );
        m_state = State::Connected;
    }
}


void NetClient::HandleUnreliable( const std::vector<uint8_t>& InMessage )
{
    BitReader reader( InMessage );
    if( m_state != State::Connected || reader.ReadBits( 8 ) != NetMessage::ServerState )
    {
        return;
    }
    const uint32_t serverTick = reader.ReadBits( 32 );
    const bool hasCommands = reader.ReadBool();
    const uint32_t newest = reader.ReadBits( 32 );
    const float slack = NetQuantize::ReadFloat( reader, -kSlackRange, kSlackRange, kSlackBits );
    // Unreliable reports can arrive out of order: only newer ones count.
    if( reader.IsOverflowed() || ( m_hasReport && serverTick <= m_serverTick ) )
    {
        return;
    }
    m_hasReport = true;
    m_serverTick = serverTick;
    if( hasCommands )
    {
        m_serverSlack = slack;
        while( !m_commands.empty() && m_commands.front().Tick <= newest )
        {
            m_commands.pop_front();
        }
        m_clock.OnSlackReport( slack );
    }
}


void NetClient::SubmitCommand( InputCommand InCommand )
{
    if( m_state != State::Connected )
    {
        return;
    }
    InCommand.Tick = m_clock.TakeTick();
    // A resync backwards re-runs ticks: their new commands replace the old.
    while( !m_commands.empty() && m_commands.back().Tick >= InCommand.Tick )
    {
        m_commands.pop_back();
    }
    m_commands.push_back( InCommand );
    // Only the newest kMaxPerPacket go out; older unconfirmed ones are past helping.
    while( m_commands.size() > NetCommands::kMaxPerPacket * 4 )
    {
        m_commands.pop_front();
    }
}


void NetClient::Flush( double InNow )
{
    if( m_state == State::Connected && !m_commands.empty() )
    {
        const size_t count = std::min( m_commands.size(), NetCommands::kMaxPerPacket );
        std::vector<InputCommand> newest( m_commands.end() - static_cast<std::ptrdiff_t>( count ), m_commands.end() );
        BitWriter writer;
        writer.WriteBits( NetMessage::Commands, 8 );
        NetCommands::Write( writer, newest.data(), newest.size() );
        m_connection.SendUnreliable( writer.GetBytes() );
    }
    if( m_state == State::Connecting || m_state == State::Connected )
    {
        m_connection.SendPacket( InNow );
    }
}
