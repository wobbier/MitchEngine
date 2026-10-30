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
    // Disconnect is unreliable (the sender is going): it goes in this many packets.
    constexpr int kDisconnectPackets = 3;

    void SendOn( NetConnection& InConnection, const BitWriter& InWriter, bool InReliable )
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

    // Disconnect, in a few packets of its own, straight away.
    void SayDisconnect( NetConnection& InConnection, const std::string& InReason, double InNow )
    {
        BitWriter message;
        message.WriteBits( NetMessage::Disconnect, 8 );
        message.WriteString( InReason );
        for( int i = 0; i < kDisconnectPackets; ++i )
        {
            InConnection.SendUnreliable( message.GetBytes() );
            InConnection.SendPacket( InNow );
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
    // Players who left or went quiet: told to whatever tracks them, then dropped.
    for( std::unique_ptr<Client>& client : m_clients )
    {
        if( client->Accepted && ( client->Left || InNow - client->LastHeard > m_timeout ) )
        {
            // On its own connection (its sequence numbers carry on), in case it's alive after all:
            // otherwise it would keep talking to a server that no longer knows it.
            if( !client->Left )
            {
                SayDisconnect( *client->Connection, "timed out", InNow );
            }
            ++( client->Left ? m_disconnectCount : m_timeoutCount );
            if( OnClientRemoved )
            {
                OnClientRemoved( *client );
            }
            client->Accepted = false;
            client->Left = true;
        }
    }
    // Rejected clients go once their Reject has been delivered (or they stop listening).
    m_clients.erase( std::remove_if( m_clients.begin(), m_clients.end(), [InNow]( const std::unique_ptr<Client>& InClient ) {
        if( InClient->Left )
        {
            return true;
        }
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
    const uint8_t type = static_cast<uint8_t>( reader.ReadBits( 8 ) );
    if( type != NetMessage::Hello )
    {
        if( InClient.Accepted && OnMessage )
        {
            OnMessage( InClient, type, reader, true );
        }
        return;
    }
    if( InClient.Accepted || InClient.Rejected )
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
    const uint8_t id = reason.empty() ? AllocateId() : 0;
    if( reason.empty() && id == 0 )
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
        ++m_rejectedCount;
    }
    else
    {
        InClient.Accepted = true;
        InClient.Id = id;
        InClient.AcceptedAt = InClient.LastHeard;
        ++m_acceptedCount;
        reply.WriteBits( NetMessage::Welcome, 8 );
        reply.WriteBits( InClient.Id, 8 );
        reply.WriteBits( m_tickRate, 16 );
        reply.WriteBits( m_tick, 32 );
    }
    SendOn( *InClient.Connection, reply, true );
    if( InClient.Accepted && OnClientAccepted )
    {
        OnClientAccepted( InClient );
    }
}


uint8_t NetServer::AllocateId()
{
    for( int attempt = 0; attempt < 255; ++attempt )
    {
        const uint8_t id = m_nextId;
        m_nextId = static_cast<uint8_t>( m_nextId == 255 ? 1 : m_nextId + 1 );
        if( FindClient( id ) == nullptr )
        {
            return id;
        }
    }
    return 0;
}


void NetServer::Send( Client& InClient, const BitWriter& InMessage, bool InReliable )
{
    SendOn( *InClient.Connection, InMessage, InReliable );
}


void NetServer::Close( double InNow, const std::string& InReason )
{
    for( std::unique_ptr<Client>& client : m_clients )
    {
        if( client->Accepted )
        {
            SayDisconnect( *client->Connection, InReason, InNow );
            if( OnClientRemoved )
            {
                OnClientRemoved( *client );
            }
        }
    }
    m_clients.clear();
}


void NetServer::HandleUnreliable( Client& InClient, const std::vector<uint8_t>& InMessage )
{
    BitReader reader( InMessage );
    const uint8_t type = static_cast<uint8_t>( reader.ReadBits( 8 ) );
    if( type == NetMessage::Disconnect )
    {
        InClient.Left = true;
        return;
    }
    if( !InClient.Accepted )
    {
        return;
    }
    if( type != NetMessage::Commands )
    {
        if( OnMessage )
        {
            OnMessage( InClient, type, reader, false );
        }
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
            SendOn( *client->Connection, state, false );
            if( OnPacket )
            {
                OnPacket( *client );
            }
        }
        // Not to addresses that never said hello (a client from a session the server has dropped):
        // they'd take the packets for their old session's and never notice it's gone.
        if( client->Accepted || client->Rejected )
        {
            client->Connection->SendPacket( InNow );
        }
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
    if( m_started )
    {
        return;
    }
    m_started = true;
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
        m_lastHeard = InNow;
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
    if( ( m_state == State::Connecting || m_state == State::Connected ) && m_lastHeard >= 0.0 && InNow - m_lastHeard > m_timeout )
    {
        m_state = State::TimedOut;
        m_endedAt = InNow;
    }
}


void NetClient::Disconnect( double InNow )
{
    if( m_state != State::Connecting && m_state != State::Connected )
    {
        return;
    }
    SayDisconnect( m_connection, "left", InNow );
    m_state = State::Disconnected;
    m_disconnectReason = "left";
    m_endedAt = InNow;
}


void NetClient::Send( const BitWriter& InMessage, bool InReliable )
{
    if( m_state == State::Connected )
    {
        SendOn( m_connection, InMessage, InReliable );
    }
}


void NetClient::HandleReliable( const std::vector<uint8_t>& InMessage, double InNow )
{
    BitReader reader( InMessage );
    const uint32_t type = reader.ReadBits( 8 );
    if( m_state == State::Connected && type != NetMessage::Welcome && type != NetMessage::Reject )
    {
        if( OnMessage )
        {
            OnMessage( static_cast<uint8_t>( type ), reader, true );
        }
        return;
    }
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
        m_connectedAt = InNow;
    }
}


void NetClient::HandleUnreliable( const std::vector<uint8_t>& InMessage )
{
    BitReader reader( InMessage );
    const uint8_t type = static_cast<uint8_t>( reader.ReadBits( 8 ) );
    if( m_state != State::Connected )
    {
        return;
    }
    if( type == NetMessage::Disconnect )
    {
        m_disconnectReason = reader.ReadString( 256 );
        m_state = State::Disconnected;
        m_endedAt = m_lastHeard;
        return;
    }
    if( type != NetMessage::ServerState )
    {
        if( OnMessage )
        {
            OnMessage( type, reader, false );
        }
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
    m_newCommand = true;
    // Only the newest kMaxPerPacket go out; older unconfirmed ones are past helping.
    while( m_commands.size() > NetCommands::kMaxPerPacket * 4 )
    {
        m_commands.pop_front();
    }
}


bool NetClient::IsSendDue( double InNow ) const
{
    if( m_state != State::Connecting && m_state != State::Connected )
    {
        return false;
    }
    // Connected, each tick's command sends; a packet without one only covers stretches with no
    // ticks (a pause, a hitch). Connecting, the handshake goes out about once a tick.
    const double tick = 1.0 / static_cast<double>( std::max<uint16_t>( m_tickRate, 1 ) );
    const double keepAlive = m_state == State::Connected ? 2.0 * tick : 0.9 * tick;
    return m_newCommand || m_lastSentAt < 0.0 || InNow - m_lastSentAt >= keepAlive;
}


void NetClient::Flush( double InNow )
{
    if( !IsSendDue( InNow ) )
    {
        return;
    }
    m_newCommand = false;
    m_lastSentAt = InNow;
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
        // The silence clock starts when the Hello first goes out.
        if( m_lastHeard < 0.0 )
        {
            m_lastHeard = InNow;
        }
    }
}
