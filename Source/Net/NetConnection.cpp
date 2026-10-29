#include "PCH.h"
#include "Net/NetConnection.h"
#include "Net/BitStream.h"
#include <algorithm>
#include <cmath>

namespace
{
    // Ack delay travels in 0.1 ms units (16 bits: up to 6.5 s).
    constexpr double kAckDelayUnit = 0.0001;
    // Header bits: protocol, sequence, has-ack, ack, ack bits, ack delay.
    constexpr size_t kHeaderBytes = ( 16 + 16 + 1 + 16 + 32 + 16 + 7 ) / 8;
    // Per message: more, reliable, id, the length (VarUInt up to 2 bytes for <= 1024).
    constexpr size_t kMessageOverheadBytes = 6;
}


NetConnection::NetConnection( INetTransport& InTransport, NetAddress InRemote )
    : m_transport( InTransport )
    , m_remote( InRemote )
{
}


bool NetConnection::SendReliable( const void* InData, size_t InSize )
{
    if( InSize > kMaxMessageBytes )
    {
        return false;
    }
    OutgoingReliable message;
    message.Id = m_nextReliableId++;
    const uint8_t* bytes = static_cast<const uint8_t*>( InData );
    message.Data.assign( bytes, bytes + InSize );
    m_outgoing.push_back( std::move( message ) );
    ++m_stats.ReliableSent;
    return true;
}


bool NetConnection::SendUnreliable( const void* InData, size_t InSize )
{
    if( InSize > kMaxMessageBytes )
    {
        return false;
    }
    const uint8_t* bytes = static_cast<const uint8_t*>( InData );
    m_unreliable.emplace_back( bytes, bytes + InSize );
    return true;
}


double NetConnection::ResendDelay() const
{
    // A little over a round trip once there's an estimate; a cautious 100 ms before.
    if( m_stats.RoundTripMs <= 0.f )
    {
        return 0.1;
    }
    return std::max( 0.02, ( m_stats.RoundTripMs * 1.25 + m_stats.RoundTripDeviationMs * 2.0 ) / 1000.0 );
}


void NetConnection::SendPacket( double InNow )
{
    const uint16_t sequence = m_sequence++;
    SentPacket& record = m_sent[sequence % m_sent.size()];
    record = SentPacket();
    record.Sequence = sequence;
    record.Valid = true;
    record.SentAt = InNow;

    BitWriter writer;
    writer.WriteBits( kProtocolId, 16 );
    writer.WriteBits( sequence, 16 );
    writer.WriteBool( m_hasReceived );
    if( m_hasReceived )
    {
        writer.WriteBits( m_remoteSequence, 16 );
        writer.WriteBits( m_receivedBits, 32 );
        const double delay = std::clamp( ( InNow - m_remoteSequenceAt ) / kAckDelayUnit, 0.0, 65535.0 );
        writer.WriteBits( static_cast<uint32_t>( std::lround( delay ) ), 16 );
    }

    size_t budget = kMaxPacketBytes - kHeaderBytes;
    auto writeMessage = [&]( bool InReliable, uint16_t InId, const std::vector<uint8_t>& InData ) {
        writer.WriteBool( true );
        writer.WriteBool( InReliable );
        if( InReliable )
        {
            writer.WriteBits( InId, 16 );
        }
        writer.WriteVarUInt( InData.size() );
        writer.WriteBytes( InData.data(), InData.size() );
        budget -= InData.size() + kMessageOverheadBytes;
    };

    // Reliable messages that are new or overdue, oldest first, inside the window.
    const double resendDelay = ResendDelay();
    const uint16_t windowStart = m_outgoing.empty() ? 0 : m_outgoing.front().Id;
    for( OutgoingReliable& message : m_outgoing )
    {
        if( static_cast<uint16_t>( message.Id - windowStart ) >= kReliableWindow )
        {
            break;
        }
        if( message.LastSent >= 0.0 && InNow - message.LastSent < resendDelay )
        {
            continue;
        }
        if( message.Data.size() + kMessageOverheadBytes > budget )
        {
            break;
        }
        if( message.LastSent >= 0.0 )
        {
            ++m_stats.ReliableResent;
        }
        message.LastSent = InNow;
        record.Reliable.push_back( message.Id );
        writeMessage( true, message.Id, message.Data );
    }
    // Unreliable messages go in this packet or not at all.
    for( const std::vector<uint8_t>& message : m_unreliable )
    {
        if( message.size() + kMessageOverheadBytes <= budget )
        {
            writeMessage( false, 0, message );
        }
    }
    m_unreliable.clear();
    writer.WriteBool( false );

    const std::vector<uint8_t>& bytes = writer.GetBytes();
    m_transport.Send( m_remote, bytes.data(), bytes.size() );
    ++m_stats.PacketsSent;
    m_stats.BytesSent += bytes.size();
}


bool NetConnection::RecordReceived( uint16_t InSequence, double InNow )
{
    if( !m_hasReceived )
    {
        m_hasReceived = true;
        m_remoteSequence = InSequence;
        m_receivedBits = 0;
        m_remoteSequenceAt = InNow;
        return true;
    }
    if( NetSequenceGreater( InSequence, m_remoteSequence ) )
    {
        const uint32_t shift = static_cast<uint16_t>( InSequence - m_remoteSequence );
        // Bit i: m_remoteSequence - 1 - i was received.
        const uint64_t bits = shift > 32 ? 0 : ( ( static_cast<uint64_t>( m_receivedBits ) << shift ) | ( uint64_t( 1 ) << ( shift - 1 ) ) );
        m_receivedBits = static_cast<uint32_t>( bits );
        m_remoteSequence = InSequence;
        m_remoteSequenceAt = InNow;
        return true;
    }
    const uint32_t age = static_cast<uint16_t>( m_remoteSequence - InSequence );
    if( age == 0 || age > 32 )
    {
        // Seen it, or too old to ack (anything reliable in it gets resent).
        return false;
    }
    const uint32_t bit = 1u << ( age - 1 );
    if( m_receivedBits & bit )
    {
        return false;
    }
    m_receivedBits |= bit;
    return true;
}


void NetConnection::OnAcked( SentPacket& InPacket )
{
    InPacket.Acked = true;
    ++m_stats.PacketsAcked;
    for( const uint16_t id : InPacket.Reliable )
    {
        auto it = std::find_if( m_outgoing.begin(), m_outgoing.end(), [id]( const OutgoingReliable& InMessage ) { return InMessage.Id == id; } );
        if( it != m_outgoing.end() )
        {
            m_outgoing.erase( it );
        }
    }
}


void NetConnection::ProcessAcks( uint16_t InAck, uint32_t InAckBits, double InAckDelay, double InNow )
{
    auto find = [this]( uint16_t InSequence ) -> SentPacket* {
        SentPacket& record = m_sent[InSequence % m_sent.size()];
        return record.Valid && record.Sequence == InSequence ? &record : nullptr;
    };
    if( SentPacket* latest = find( InAck ); latest && !latest->Acked )
    {
        // The round trip of the newest packet acked, minus the time it waited on the far side.
        const double sample = std::max( 0.0, InNow - latest->SentAt - InAckDelay ) * 1000.0;
        if( m_stats.RoundTripMs <= 0.f )
        {
            m_stats.RoundTripMs = static_cast<float>( sample );
            m_stats.RoundTripDeviationMs = static_cast<float>( sample * 0.5 );
        }
        else
        {
            const double error = sample - m_stats.RoundTripMs;
            m_stats.RoundTripDeviationMs += static_cast<float>( ( std::abs( error ) - m_stats.RoundTripDeviationMs ) * 0.25 );
            m_stats.RoundTripMs += static_cast<float>( error * 0.125 );
        }
        OnAcked( *latest );
    }
    for( uint32_t i = 0; i < 32; ++i )
    {
        if( InAckBits & ( 1u << i ) )
        {
            if( SentPacket* record = find( static_cast<uint16_t>( InAck - 1 - i ) ); record && !record->Acked )
            {
                OnAcked( *record );
            }
        }
    }

    // Packets that fell out of the ack window unacked are lost.
    if( !m_hasAck || NetSequenceGreater( InAck, m_latestAck ) )
    {
        m_hasAck = true;
        m_latestAck = InAck;
        const uint16_t settled = static_cast<uint16_t>( InAck - 33 );
        while( NetSequenceGreater( settled, m_finalized ) )
        {
            ++m_finalized;
            if( SentPacket* record = find( m_finalized ) )
            {
                if( !record->Acked )
                {
                    ++m_stats.PacketsLost;
                }
                m_stats.PacketLoss += ( ( record->Acked ? 0.f : 1.f ) - m_stats.PacketLoss ) * 0.05f;
            }
        }
    }
}


void NetConnection::AcceptReliable( uint16_t InId, std::vector<uint8_t>&& InData )
{
    // Already delivered, or already waiting for the ones before it.
    if( !NetSequenceGreater( InId, static_cast<uint16_t>( m_nextDeliverId - 1 ) ) || m_early.count( InId ) )
    {
        return;
    }
    m_early.emplace( InId, std::move( InData ) );
    for( auto it = m_early.find( m_nextDeliverId ); it != m_early.end(); it = m_early.find( m_nextDeliverId ) )
    {
        m_reliableIn.push_back( std::move( it->second ) );
        m_early.erase( it );
        ++m_nextDeliverId;
        ++m_stats.ReliableDelivered;
    }
}


bool NetConnection::ReceivePacket( const uint8_t* InData, size_t InSize, double InNow )
{
    BitReader reader( InData, InSize );
    if( reader.ReadBits( 16 ) != kProtocolId )
    {
        ++m_stats.PacketsRejected;
        return false;
    }
    const uint16_t sequence = static_cast<uint16_t>( reader.ReadBits( 16 ) );
    const bool hasAck = reader.ReadBool();
    uint16_t ack = 0;
    uint32_t ackBits = 0;
    double ackDelay = 0.0;
    if( hasAck )
    {
        ack = static_cast<uint16_t>( reader.ReadBits( 16 ) );
        ackBits = reader.ReadBits( 32 );
        ackDelay = reader.ReadBits( 16 ) * kAckDelayUnit;
    }

    // Parse everything before acting on any of it: a malformed packet changes nothing.
    struct Message
    {
        bool Reliable = false;
        uint16_t Id = 0;
        std::vector<uint8_t> Data;
    };
    std::vector<Message> messages;
    while( reader.ReadBool() )
    {
        Message message;
        message.Reliable = reader.ReadBool();
        if( message.Reliable )
        {
            message.Id = static_cast<uint16_t>( reader.ReadBits( 16 ) );
        }
        const uint64_t size = reader.ReadVarUInt();
        if( reader.IsOverflowed() || size > kMaxMessageBytes )
        {
            break;
        }
        message.Data.resize( static_cast<size_t>( size ) );
        reader.ReadBytes( message.Data.data(), message.Data.size() );
        messages.push_back( std::move( message ) );
    }
    if( reader.IsOverflowed() )
    {
        ++m_stats.PacketsRejected;
        return false;
    }
    if( !RecordReceived( sequence, InNow ) )
    {
        ++m_stats.DuplicatesDropped;
        return false;
    }
    ++m_stats.PacketsReceived;
    m_stats.BytesReceived += InSize;
    if( hasAck )
    {
        ProcessAcks( ack, ackBits, ackDelay, InNow );
    }
    for( Message& message : messages )
    {
        if( message.Reliable )
        {
            AcceptReliable( message.Id, std::move( message.Data ) );
        }
        else
        {
            m_unreliableIn.push_back( std::move( message.Data ) );
        }
    }
    return true;
}


bool NetConnection::ReceiveReliable( std::vector<uint8_t>& OutMessage )
{
    if( m_reliableIn.empty() )
    {
        return false;
    }
    OutMessage = std::move( m_reliableIn.front() );
    m_reliableIn.pop_front();
    return true;
}


bool NetConnection::ReceiveUnreliable( std::vector<uint8_t>& OutMessage )
{
    if( m_unreliableIn.empty() )
    {
        return false;
    }
    OutMessage = std::move( m_unreliableIn.front() );
    m_unreliableIn.pop_front();
    return true;
}
