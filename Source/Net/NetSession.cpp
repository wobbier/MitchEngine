#include "PCH.h"
#include "Net/NetSession.h"
#include "CLog.h"
#include "Net/BitStream.h"
#include "Net/NetQuantize.h"
#include <algorithm>
#include <chrono>
#include <cmath>

namespace
{
    // A rejected client keeps its connection this long, so the Reject gets through.
    constexpr double kRejectLinger = 2.0;
    constexpr double kHandshakeTimeout = 10.0;
    constexpr float kSlackRange = 64.f;
    constexpr int kSlackBits = 14;
    // A report's wait for the client's commands, in ms, up to a minute.
    constexpr double kMaxWaitMs = 60000.0;
    // Disconnect is unreliable (the sender is going): it goes in this many packets.
    constexpr int kDisconnectPackets = 3;

    void SendOn( NetConnection& InConnection, const BitWriter& InWriter, bool InReliable )
    {
        const bool sent = InReliable ? InConnection.SendReliable( InWriter.GetBytes() ) : InConnection.SendUnreliable( InWriter.GetBytes() );
        if( !sent )
        {
            // Too big for one message (a pawn with a lot of predicted state, say): say so, once a
            // second, instead of dropping it silently and never syncing.
            static auto lastWarning = std::chrono::steady_clock::time_point();
            const auto now = std::chrono::steady_clock::now();
            if( now - lastWarning >= std::chrono::seconds( 1 ) )
            {
                lastWarning = now;
                YIKES( "Net: dropped a " + std::to_string( InWriter.GetBytes().size() ) + "-byte " + ( InReliable ? "reliable" : "unreliable" )
                    + " message; the limit is " + std::to_string( NetConnection::kMaxMessageBytes ) + " bytes" );
            }
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
    m_maxCommandRepeats = static_cast<uint32_t>( std::max( 1L, std::lround( kDefaultCommandRepeatSeconds * InTickRate ) ) );
}


void NetServer::SetMaxCommandRepeats( uint32_t InTicks )
{
    m_maxCommandRepeats = InTicks;
    for( std::unique_ptr<Client>& client : m_clients )
    {
        client->Commands.SetMaxRepeats( InTicks );
    }
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
    client->Commands.SetMaxRepeats( m_maxCommandRepeats );
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
            HandleUnreliable( *client, message, InNow );
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
        // MOD-3: which mods.
        if( const std::string mods = m_version.DescribeModDifferences( theirs ); !mods.empty() )
        {
            reason += " (" + mods + ")";
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
        // Where it will be once caught up (after a hitch): the client's clock starts from there.
        reply.WriteBits( m_tick + m_ticksOwed, 32 );
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


void NetServer::HandleUnreliable( Client& InClient, const std::vector<uint8_t>& InMessage, double InNow )
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
    // Packets can arrive out of order: only the latest one sent speaks for the client's clock now
    // (a keep-alive moves "latest" on too, so this holds however long the client idled).
    const bool latest = InClient.Connection->WasLastPacketNewest();
    InClient.Commands.ReceivePacket( commands.data(), commands.size(), m_tick, latest );
    // A new command heard (not a keep-alive repeating the last packet's): the wait starts over.
    if( latest && !commands.empty() && ( InClient.CommandsAt < 0.0 || commands.back().Tick != InClient.CommandsEnd ) )
    {
        InClient.CommandsAt = InNow;
        InClient.CommandsEnd = commands.back().Tick;
    }
}


uint32_t NetServer::GetMaxTicksOwed() const
{
    return static_cast<uint32_t>( std::lround( kMaxCatchUpSeconds * m_tickRate ) );
}


void NetServer::BeginTick( uint32_t InTicksOwed )
{
    m_ticksOwed = std::min( InTicksOwed, GetMaxTicksOwed() );
    for( std::unique_ptr<Client>& client : m_clients )
    {
        if( client->Accepted )
        {
            client->Previous = client->Current;
            client->Current = client->Commands.Consume( m_tick, m_ticksOwed );
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
            // The slack, quantized within +/-kSlackRange; further off (a client paused for minutes,
            // a server that lost seconds to a hitch), in whole ticks, in full.
            const float slack = client->Commands.GetSlack();
            const bool quantized = std::abs( slack ) < kSlackRange;
            state.WriteBool( quantized );
            if( quantized )
            {
                NetQuantize::WriteFloat( state, slack, -kSlackRange, kSlackRange, kSlackBits );
            }
            else
            {
                const double ticks = std::clamp( std::round( static_cast<double>( slack ) ), -2147483647.0, 2147483647.0 );
                state.WriteBits( static_cast<uint32_t>( static_cast<int32_t>( ticks ) ), 32 );
            }
            state.WriteVarUInt( client->Commands.GetMissedCount() );
            // How long it has waited for this client's commands: the client's slack sinks with a
            // wait it can't see from its side (an uplink spike, its own hitch), and mustn't steer on it.
            const double waitMs = client->CommandsAt >= 0.0 ? std::clamp( ( InNow - client->CommandsAt ) * 1000.0, 0.0, kMaxWaitMs ) : 0.0;
            state.WriteVarUInt( static_cast<uint64_t>( std::lround( waitMs ) ) );
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
    m_jitterBefore = m_connection.GetStats().RoundTripDeviationMs / 1000.0;
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
    // Every report was noted as it came; only the newest of the batch steers (the ones before it
    // are older news).
    if( m_hasNewReport && m_state == State::Connected )
    {
        Steer( m_report );
    }
    m_hasNewReport = false;
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
        m_rejectReason = reader.ReadText( 256 );
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
        // (so commands arrive in time) plus the slack. Its ticks are the server's length.
        const double rttTicks = m_connection.GetStats().RoundTripMs / 1000.0 * m_tickRate;
        m_clock.SetTickSeconds( 1.0 / static_cast<double>( m_tickRate ) );
        m_clock.Start( serverTick + static_cast<uint32_t>( std::ceil( rttTicks + m_clock.GetTargetSlack() ) ) );
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
        m_disconnectReason = reader.ReadText( 256 );
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
    Report report;
    report.ServerTick = reader.ReadBits( 32 );
    report.HasCommands = reader.ReadBool();
    report.Newest = reader.ReadBits( 32 );
    report.Slack = reader.ReadBool() ? NetQuantize::ReadFloat( reader, -kSlackRange, kSlackRange, kSlackBits ) : static_cast<float>( static_cast<int32_t>( reader.ReadBits( 32 ) ) );
    report.Misses = reader.ReadVarUInt();
    report.Waited = static_cast<double>( reader.ReadVarUInt() ) / 1000.0;
    report.HasAck = m_connection.GetLastPacketAck( report.Ack );
    // Unreliable reports can arrive out of order: only newer ones count.
    if( reader.IsOverflowed() || ( m_hasReport && report.ServerTick <= m_serverTick ) )
    {
        return;
    }
    m_hasReport = true;
    m_serverTick = report.ServerTick;
    NoteReport( report );
    m_report = report;
    m_hasNewReport = true;
}


double NetClient::GetUsualWait() const
{
    // One send interval (smoothed, at least a tick), plus the two loops' phase and the link's
    // jitter. The jitter counts two ticks at most: a latency spike inflates the round trip's
    // deviation for a while, and mustn't pass the wait it caused off as usual.
    const double tick = 1.0 / static_cast<double>( std::max<uint16_t>( m_tickRate, 1 ) );
    return std::max( tick, m_sendInterval ) + 2.0 * tick + std::min( 2.0 * m_jitterBefore, 2.0 * tick );
}


bool NetClient::IsStalled( const Report& InReport ) const
{
    // The server says how long it had waited for this client's commands when it sent the report:
    // well past the usual, its slack has sunk with the wait, not the clock.
    return InReport.Waited > GetUsualWait();
}


void NetClient::NoteReport( const Report& InReport )
{
    m_clock.SetJitter( m_connection.GetStats().RoundTripDeviationMs / 1000.0 );
    // Repeats since the last report (the first only sets the baseline).
    const uint64_t misses = m_hasMissReport && InReport.Misses > m_lastMisses ? InReport.Misses - m_lastMisses : 0;
    m_hasMissReport = true;
    m_lastMisses = std::max( m_lastMisses, InReport.Misses );
    if( !InReport.HasCommands )
    {
        return;
    }
    m_serverSlack = InReport.Slack;
    // After a resync the reports describe the old clock until the server has the first packet
    // with a command made since (this packet's ack reaches it). Their misses are the old clock's:
    // the jump fixed those, a deeper buffer wouldn't have. And a newest from before a jump back
    // confirms nothing: the client made those ticks again.
    if( m_awaitingResync )
    {
        if( !m_hasResyncSequence || !InReport.HasAck || NetSequenceGreater( m_resyncSequence, InReport.Ack ) )
        {
            return;
        }
        m_awaitingResync = false;
        m_hasNewest = false;
    }
    // Confirmed: the server holds them (never a newest past what this client has made).
    const uint32_t made = m_clock.GetTick();
    if( InReport.Newest < made )
    {
        while( !m_commands.empty() && m_commands.front().Tick <= InReport.Newest )
        {
            m_commands.pop_front();
        }
    }
    // Misses come in gaps: the server waiting for this client's next command (between packets,
    // a burst of loss or latency, an outage). They wait in m_gapMisses until the gap ends. One no
    // buffer could cover is an outage: the server waited past the usual wait plus MaxSlack (or,
    // when its reports from the gap were lost, it missed more than that less the buffer it ran
    // down first). Its misses don't grow the target: the repeats cover an outage, and a deeper
    // buffer wouldn't have.
    const bool moved = !m_hasNewest || InReport.Newest != m_lastNewest;
    m_hasNewest = true;
    m_lastNewest = InReport.Newest;
    const double usual = GetUsualWait();
    const double tick = 1.0 / static_cast<double>( std::max<uint16_t>( m_tickRate, 1 ) );
    const double limit = usual + m_clock.GetSettings().MaxSlack * tick;
    m_gapMisses += misses;
    if( !m_inOutage && ( InReport.Waited > limit || ( static_cast<double>( m_gapMisses ) + m_clock.GetTargetSlack() ) * tick > limit ) )
    {
        m_inOutage = true;
        ++m_outages;
    }
    if( m_inOutage )
    {
        m_gapMisses = 0;
    }
    if( moved && InReport.Waited <= usual )
    {
        // The server hears this client again (a newer command, just now): the gap, if any, ended,
        // and a buffer that much deeper would have covered it. (A newest that moved long ago, from
        // a packet already on its way when the gap began, doesn't end it.)
        m_dueMisses += m_gapMisses;
        m_gapMisses = 0;
        m_inOutage = false;
    }
}


void NetClient::Steer( const Report& InReport )
{
    if( !InReport.HasCommands || m_awaitingResync )
    {
        return;
    }
    const uint32_t due = static_cast<uint32_t>( std::min<uint64_t>( m_dueMisses, 64 ) );
    m_dueMisses = 0;
    if( IsStalled( InReport ) )
    {
        // The server isn't hearing this client: its slack falls with the silence, not the clock.
        // Hold the clock until commands get through again.
        ++m_stalledReports;
        m_stalls += m_stalled ? 0 : 1;
        m_stalled = true;
        m_clock.OnStall();
        m_clock.OnMisses( due );
        return;
    }
    m_stalled = false;
    // The slack the newest command arrived with: against the report's tick, plus the ticks the
    // server has waited since. Unsmoothed, and steady through a gap's first ticks, before the
    // report counts as stalled (the slack sinks meanwhile). It leaves out a catching-up server's
    // ticks owed, so it can read ahead of the slack, never behind it.
    const double tickRate = static_cast<double>( std::max<uint16_t>( m_tickRate, 1 ) );
    const float arrived = static_cast<float>( static_cast<double>( static_cast<int64_t>( InReport.Newest ) - static_cast<int64_t>( InReport.ServerTick ) ) + InReport.Waited * tickRate );
    const int resyncs = m_clock.GetResyncCount();
    m_clock.OnSlackReport( InReport.Slack, arrived );
    if( m_clock.GetResyncCount() != resyncs )
    {
        // The clock was off (its commands late or far early): the jump fixes that, a deeper
        // buffer wouldn't. The reports describe the old clock until the server hears the new (and
        // the clock ignores their misses until it can steer again: NetClock::OnMisses).
        m_awaitingResync = true;
        m_resyncCommandMade = false;
        m_hasResyncSequence = false;
        m_gapMisses = 0;
        m_inOutage = false;
        return;
    }
    // The buffer grows to cover them (NET-14).
    m_clock.OnMisses( due );
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
    // A resync forwards skips ticks. The server would repeat the last command through the ones
    // still ahead of it, as misses that grow the slack target; send it for them instead (the last
    // packet's worth), so the stream it reads runs on unbroken.
    if( m_hasLastCommand && InCommand.Tick > m_lastCommand.Tick + 1 )
    {
        const uint32_t window = static_cast<uint32_t>( NetCommands::kMaxPerPacket - 1 );
        const uint32_t from = std::max( m_lastCommand.Tick + 1, InCommand.Tick > window ? InCommand.Tick - window : 0u );
        for( uint32_t tick = from; tick < InCommand.Tick; ++tick )
        {
            InputCommand filler = m_lastCommand;
            filler.Tick = tick;
            m_commands.push_back( filler );
        }
    }
    m_commands.push_back( InCommand );
    m_lastCommand = InCommand;
    m_hasLastCommand = true;
    m_newCommand = true;
    m_resyncCommandMade = m_resyncCommandMade || m_awaitingResync;
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
    if( m_newCommand )
    {
        // How often new commands go out: a stall is a wait well past it. (A hitch's gap counts a
        // quarter second at most: it's no guide to the usual.)
        if( m_lastCommandSentAt >= 0.0 )
        {
            m_sendInterval += ( std::clamp( InNow - m_lastCommandSentAt, 0.0, 0.25 ) - m_sendInterval ) * 0.1;
        }
        m_lastCommandSentAt = InNow;
    }
    m_newCommand = false;
    m_lastSentAt = InNow;
    if( m_state == State::Connected && !m_commands.empty() )
    {
        const size_t count = std::min( m_commands.size(), NetCommands::kMaxPerPacket );
        std::vector<InputCommand> newest( m_commands.end() - static_cast<std::ptrdiff_t>( count ), m_commands.end() );
        // The first packet with a command made since a resync: a report in a packet that acks it
        // describes the new clock.
        if( m_awaitingResync && m_resyncCommandMade && !m_hasResyncSequence )
        {
            m_resyncSequence = m_connection.GetNextSequence();
            m_hasResyncSequence = true;
        }
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
