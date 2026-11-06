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
    // A packet handled long after this end last sent waited on it (a hitch, a breakpoint): when it
    // really arrived is unknown, so it says nothing about the round trip. Long: past this, or three
    // of this end's usual gaps between packets (a slow end sends seldom, and must still sample).
    constexpr double kMaxRoundTripWait = 0.05;
    constexpr double kRoundTripWaitGaps = 3.0;
    // Or read over this many of this end's usual gaps between reads after the one before.
    constexpr double kReadWaitGaps = 2.0;
    // A gap between packets counts this much at most towards the usual one (a hitch's is no guide).
    constexpr double kMaxSendGapSample = 0.5;

    // Blobs (ME-323). The trailer after the messages: its flag, extra-packet bit and the two lists' ends.
    constexpr size_t kTrailerBytes = 1;
    // Per cancel: more, id (VarUInt, up to 5 bytes), theirs.
    constexpr size_t kCancelBytes = 7;
    // Per slice: more, id (up to 5 bytes), size (up to 4 for 4 MB), index (up to 2 for 4096 slices).
    constexpr size_t kSliceOverheadBytes = 12;
    // The buckets hold this many seconds of their rate (at least a packet or two), and owe at most
    // as much: a burst of slice packets is a couple of ticks' worth at 60 Hz.
    constexpr double kBlobBurstSeconds = 0.035;
    // Ids further than this past the oldest one not done are ignored (a hostile peer's).
    constexpr uint32_t kMaxBlobIdAhead = 1u << 20;
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


void NetConnection::BeginRead( double InNow )
{
    m_readGap = m_lastReadAt >= 0.0 ? InNow - m_lastReadAt : 0.0;
    m_lastReadAt = InNow;
    if( m_readGap > 0.0 )
    {
        const double sample = std::min( m_readGap, kMaxSendGapSample );
        m_readInterval = m_readInterval < 0.0 ? sample : m_readInterval + ( sample - m_readInterval ) * 0.1;
    }
}


void NetConnection::SendPacket( double InNow )
{
    RefillBlobTokens( InNow );
    const uint16_t sequence = m_sequence++;
    SentPacket& record = m_sent[sequence % m_sent.size()];
    record = SentPacket();
    record.Sequence = sequence;
    record.Valid = true;
    record.SentAt = InNow;
    if( m_lastSentAt >= 0.0 )
    {
        m_sendGap += ( std::clamp( InNow - m_lastSentAt, 0.0, kMaxSendGapSample ) - m_sendGap ) * 0.1;
    }
    m_lastSentAt = InNow;

    BitWriter writer;
    WriteHeader( writer, sequence, InNow );

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

    // Reliable messages that are new or overdue, oldest first, inside the window. They leave room for
    // the largest unreliable message queued this tick: a late joiner's burst of Spawns used to fill
    // packets for several ticks and starve snapshots and owner state, which go in this packet or not at all.
    size_t reserved = 0;
    for( const std::vector<uint8_t>& message : m_unreliable )
    {
        reserved = std::max( reserved, message.size() + kMessageOverheadBytes );
    }
    reserved = std::min( reserved, budget );
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
        // The first reliable message of a packet ignores the reservation: a big unreliable message
        // every tick must never hold the head of the reliable stream (and everything behind it) forever.
        const size_t floor = record.Reliable.empty() ? 0 : reserved;
        if( message.Data.size() + kMessageOverheadBytes + floor > budget )
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

    // Blobs: cancels due, and slices in the room left (after everything live, which they never displace).
    budget = budget > kTrailerBytes ? budget - kTrailerBytes : 0;
    WriteBlobTrailer( writer, false, budget, InNow, record );

    const std::vector<uint8_t>& bytes = writer.GetBytes();
    m_transport.Send( m_remote, bytes.data(), bytes.size() );
    ++m_stats.PacketsSent;
    m_stats.BytesSent += bytes.size();
    m_blobLinkTokens -= static_cast<double>( bytes.size() );

    // Then packets of slices alone, while the share allows.
    for( int i = 0; i < kMaxBlobPacketsPerSend && HasBlobAllowance() && HasSliceDue( InNow, resendDelay ) && SendBlobPacket( InNow ); ++i )
    {
    }
    const double linkFloor = -std::max( 2.0 * kMaxPacketBytes, m_blobSettings.LinkBytesPerSecond * kBlobBurstSeconds );
    m_blobLinkTokens = std::max( m_blobLinkTokens, linkFloor );
}


void NetConnection::WriteHeader( BitWriter& InWriter, uint16_t InSequence, double InNow ) const
{
    InWriter.WriteBits( kProtocolId, 16 );
    InWriter.WriteBits( InSequence, 16 );
    InWriter.WriteBool( m_hasReceived );
    if( m_hasReceived )
    {
        InWriter.WriteBits( m_remoteSequence, 16 );
        InWriter.WriteBits( m_receivedBits, 32 );
        const double delay = std::clamp( ( InNow - m_remoteSequenceAt ) / kAckDelayUnit, 0.0, 65535.0 );
        InWriter.WriteBits( static_cast<uint32_t>( std::lround( delay ) ), 16 );
    }
}


bool NetConnection::SendBlobPacket( double InNow )
{
    // Built aside first: a packet the share or the room leaves without a slice isn't sent at all.
    SentPacket record;
    record.Sequence = m_sequence;
    record.Valid = true;
    record.SentAt = InNow;
    BitWriter writer;
    WriteHeader( writer, record.Sequence, InNow );
    writer.WriteBool( false );  // no messages
    size_t budget = kMaxPacketBytes - kHeaderBytes - kTrailerBytes;
    if( !WriteBlobTrailer( writer, true, budget, InNow, record ) )
    {
        return false;
    }
    ++m_sequence;
    m_sent[record.Sequence % m_sent.size()] = std::move( record );

    const std::vector<uint8_t>& bytes = writer.GetBytes();
    m_transport.Send( m_remote, bytes.data(), bytes.size() );
    ++m_stats.PacketsSent;
    ++m_stats.BlobPacketsSent;
    m_stats.BytesSent += bytes.size();
    m_blobLinkTokens -= static_cast<double>( bytes.size() );
    return true;
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
    for( const BlobSliceRef& slice : InPacket.Slices )
    {
        OnSliceAcked( slice );
    }
    for( const BlobCancel& cancel : InPacket.Cancels )
    {
        m_blobCancels.erase( std::remove_if( m_blobCancels.begin(), m_blobCancels.end(),
                                 [&cancel]( const BlobCancel& InCancel ) { return InCancel.Id == cancel.Id && InCancel.Theirs == cancel.Theirs; } ),
            m_blobCancels.end() );
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
        // The round trip of the newest packet acked, minus the time it waited on the far side. Not
        // from a packet that sat unread here while this end stalled: that wait isn't the link's.
        // (Stalled since its last send, or since its last read: a hitch after a frame's read and
        // before its send, then the send just before this read.)
        const double sample = std::max( 0.0, InNow - latest->SentAt - InAckDelay ) * 1000.0;
        const double maxWait = std::max( kMaxRoundTripWait, kRoundTripWaitGaps * m_sendGap );
        if( m_stats.RoundTripMs <= 0.f )
        {
            m_stats.RoundTripMs = static_cast<float>( sample );
            m_stats.RoundTripDeviationMs = static_cast<float>( sample * 0.5 );
        }
        else if( InNow - m_lastSentAt <= maxWait && m_readGap <= maxWait && ( m_readInterval < 0.0 || m_readGap <= kReadWaitGaps * m_readInterval ) )
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
    bool malformed = false;
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
            malformed = true;
            break;
        }
        message.Data.resize( static_cast<size_t>( size ) );
        reader.ReadBytes( message.Data.data(), message.Data.size() );
        messages.push_back( std::move( message ) );
    }
    // The blob trailer (ME-323), when there's one: an older peer's packet ends in zero padding.
    bool extra = false;
    std::vector<std::pair<uint32_t, bool>> cancels;
    std::vector<ReceivedSlice> slices;
    if( !malformed && reader.GetBitsRemaining() > 0 && reader.ReadBool() )
    {
        extra = reader.ReadBool();
        while( !malformed && reader.ReadBool() )
        {
            const uint64_t id = reader.ReadVarUInt();
            const bool theirs = reader.ReadBool();
            malformed = id == 0 || id > 0xFFFFFFFFull;
            cancels.emplace_back( static_cast<uint32_t>( id ), theirs );
        }
        while( !malformed && reader.ReadBool() )
        {
            ReceivedSlice slice;
            const uint64_t id = reader.ReadVarUInt();
            const uint64_t size = reader.ReadVarUInt();
            const uint64_t index = reader.ReadVarUInt();
            if( reader.IsOverflowed() || id == 0 || id > 0xFFFFFFFFull || size == 0 || size > kMaxBlobBytes || index >= SliceCount( static_cast<size_t>( size ) ) )
            {
                malformed = true;
                break;
            }
            slice.Blob = static_cast<uint32_t>( id );
            slice.Size = static_cast<uint32_t>( size );
            slice.Index = static_cast<uint32_t>( index );
            slice.Data.resize( SliceBytes( slice.Size, slice.Index ) );
            reader.ReadBytes( slice.Data.data(), slice.Data.size() );
            slices.push_back( std::move( slice ) );
        }
    }
    if( malformed || reader.IsOverflowed() )
    {
        ++m_stats.PacketsRejected;
        return false;
    }
    if( !RecordReceived( sequence, InNow ) )
    {
        ++m_stats.DuplicatesDropped;
        return false;
    }
    // Newest among the packets SendPacket always sends: slice packets carry no messages, and one
    // sent after a packet and arriving before it mustn't make that packet's messages old news.
    m_lastPacketNewest = !extra && ( !m_hasMainReceived || NetSequenceGreater( sequence, m_mainSequence ) );
    if( m_lastPacketNewest )
    {
        m_hasMainReceived = true;
        m_mainSequence = sequence;
    }
    ++m_stats.PacketsReceived;
    m_stats.BytesReceived += InSize;
    m_lastPacketHasAck = hasAck;
    m_lastPacketAck = ack;
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
    for( const auto& [id, theirs] : cancels )
    {
        AcceptBlobCancel( id, theirs );
    }
    for( ReceivedSlice& slice : slices )
    {
        AcceptSlice( std::move( slice ), InNow );
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



// Blobs (ME-323) ---------------------------------------------------------------------------------

uint32_t NetConnection::SendBlob( const void* InData, size_t InSize )
{
    if( InSize == 0 || InSize > kMaxBlobBytes )
    {
        ++m_stats.BlobsRefused;
        return 0;
    }
    OutgoingBlob blob;
    blob.Id = m_nextBlobId++;
    if( m_nextBlobId == 0 )
    {
        m_nextBlobId = 1;
    }
    const uint8_t* bytes = static_cast<const uint8_t*>( InData );
    blob.Data.assign( bytes, bytes + InSize );
    blob.SliceCount = SliceCount( InSize );
    blob.SliceSentAt.assign( blob.SliceCount, -1.0 );
    blob.SliceAcked.assign( blob.SliceCount, false );
    m_blobsOut.push_back( std::move( blob ) );
    ++m_stats.BlobsSent;
    return m_blobsOut.back().Id;
}


bool NetConnection::CancelBlob( uint32_t InId )
{
    auto it = std::find_if( m_blobsOut.begin(), m_blobsOut.end(), [InId]( const OutgoingBlob& InBlob ) { return InBlob.Id == InId; } );
    if( it == m_blobsOut.end() )
    {
        return false;
    }
    m_blobsOut.erase( it );
    // Told even when nothing of it went yet: the remote counts every id as done before it moves on.
    BlobCancel cancel;
    cancel.Id = InId;
    m_blobCancels.push_back( cancel );
    ++m_stats.BlobsCancelled;
    return true;
}


bool NetConnection::CancelIncomingBlob( uint32_t InId )
{
    auto it = std::find_if( m_blobsIn.begin(), m_blobsIn.end(), [InId]( const IncomingBlob& InBlob ) { return InBlob.Id == InId; } );
    if( it == m_blobsIn.end() )
    {
        return false;
    }
    m_blobsIn.erase( it );
    MarkIncomingFinished( InId );
    BlobCancel cancel;
    cancel.Id = InId;
    cancel.Theirs = true;
    m_blobCancels.push_back( cancel );
    ++m_stats.BlobsCancelled;
    return true;
}


bool NetConnection::ReceiveBlob( uint32_t& OutId, std::vector<uint8_t>& OutData )
{
    if( m_blobsDelivered.empty() )
    {
        return false;
    }
    OutId = m_blobsDelivered.front().first;
    OutData = std::move( m_blobsDelivered.front().second );
    m_blobsDelivered.pop_front();
    return true;
}


std::vector<NetBlobProgress> NetConnection::GetBlobProgress() const
{
    auto bytesOf = []( size_t InSize, uint32_t InCount, bool InHasLast ) {
        // Every slice is full but the last.
        const size_t full = static_cast<size_t>( InCount ) * kBlobSliceBytes;
        return InHasLast ? full - ( kBlobSliceBytes - SliceBytes( InSize, SliceCount( InSize ) - 1 ) ) : full;
    };
    std::vector<NetBlobProgress> out;
    const size_t slots = GetActiveBlobSlots();
    for( size_t i = 0; i < m_blobsOut.size(); ++i )
    {
        const OutgoingBlob& blob = m_blobsOut[i];
        NetBlobProgress progress;
        progress.Id = blob.Id;
        progress.Outgoing = true;
        progress.Started = i < slots;
        progress.Size = blob.Data.size();
        progress.DoneBytes = bytesOf( blob.Data.size(), blob.AckedCount, blob.SliceAcked.back() );
        progress.StartedAt = blob.StartedAt;
        out.push_back( progress );
    }
    for( const IncomingBlob& blob : m_blobsIn )
    {
        NetBlobProgress progress;
        progress.Id = blob.Id;
        progress.Outgoing = false;
        progress.Size = blob.Data.size();
        progress.DoneBytes = bytesOf( blob.Data.size(), blob.HaveCount, blob.Have.back() );
        progress.ContiguousBytes = std::min( blob.Data.size(), static_cast<size_t>( blob.Contiguous ) * kBlobSliceBytes );
        progress.StartedAt = blob.StartedAt;
        out.push_back( progress );
    }
    return out;
}


void NetConnection::RefillBlobTokens( double InNow )
{
    const double linkRate = m_blobSettings.LinkBytesPerSecond;
    const double minRate = m_blobSettings.MinBytesPerSecond;
    const double seconds = m_blobRefilledAt < 0.0 ? 0.0 : std::clamp( InNow - m_blobRefilledAt, 0.0, 0.25 );
    m_blobRefilledAt = InNow;
    m_blobLinkTokens = std::min( m_blobLinkTokens + linkRate * seconds, std::max( 2.0 * kMaxPacketBytes, linkRate * kBlobBurstSeconds ) );
    m_blobMinTokens = std::min( m_blobMinTokens + minRate * seconds, std::max( static_cast<double>( kMaxPacketBytes ), minRate * kBlobBurstSeconds ) );
}


size_t NetConnection::GetActiveBlobSlots() const
{
    // A blob this end cancelled holds its slot until the remote has heard: then the remote never
    // holds more than kMaxActiveBlobs of this end's at once.
    const size_t cancelling = static_cast<size_t>( std::count_if( m_blobCancels.begin(), m_blobCancels.end(), []( const BlobCancel& InCancel ) { return !InCancel.Theirs; } ) );
    return std::min( m_blobsOut.size(), kMaxActiveBlobs > cancelling ? kMaxActiveBlobs - cancelling : 0 );
}


bool NetConnection::HasSliceDue( double InNow, double InResendDelay ) const
{
    const size_t slots = GetActiveBlobSlots();
    for( size_t i = 0; i < slots; ++i )
    {
        const OutgoingBlob& blob = m_blobsOut[i];
        if( blob.NextNew < blob.SliceCount )
        {
            return true;
        }
        for( const uint32_t slice : blob.InFlight )
        {
            if( !blob.SliceAcked[slice] )
            {
                if( InNow - blob.SliceSentAt[slice] >= InResendDelay )
                {
                    return true;
                }
                break;  // the oldest send not acked isn't due: none after it is
            }
        }
    }
    return false;
}


bool NetConnection::TakeSlice( double InNow, double InResendDelay, size_t InRoom, BlobSliceRef& OutSlice )
{
    const size_t slots = GetActiveBlobSlots();
    for( size_t k = 0; k < slots; ++k )
    {
        const size_t i = ( m_blobTurn + k ) % slots;
        OutgoingBlob& blob = m_blobsOut[i];
        while( !blob.InFlight.empty() && blob.SliceAcked[blob.InFlight.front()] )
        {
            blob.InFlight.pop_front();
        }
        // An overdue resend first (the receiver can't finish without it), else the next new slice.
        const bool resend = !blob.InFlight.empty() && InNow - blob.SliceSentAt[blob.InFlight.front()] >= InResendDelay;
        if( !resend && blob.NextNew >= blob.SliceCount )
        {
            continue;
        }
        const uint32_t slice = resend ? blob.InFlight.front() : blob.NextNew;
        if( SliceBytes( blob.Data.size(), slice ) > InRoom )
        {
            continue;
        }
        if( resend )
        {
            blob.InFlight.pop_front();
            ++m_stats.BlobSlicesResent;
        }
        else
        {
            ++blob.NextNew;
        }
        if( blob.StartedAt < 0.0 )
        {
            blob.StartedAt = InNow;
        }
        blob.SliceSentAt[slice] = InNow;
        blob.InFlight.push_back( slice );
        m_blobTurn = i + 1;
        OutSlice.Blob = blob.Id;
        OutSlice.Index = slice;
        return true;
    }
    return false;
}


bool NetConnection::WriteBlobTrailer( BitWriter& InWriter, bool InExtra, size_t& InBudget, double InNow, SentPacket& InRecord )
{
    const double resendDelay = ResendDelay();
    // Cancels due: new ones, and any not acked for a little over a round trip. Tiny, outside the
    // share, and in the packet SendPacket always sends (a packet of slices alone carries slices only).
    for( BlobCancel& cancel : m_blobCancels )
    {
        if( !InExtra && ( cancel.LastSent < 0.0 || InNow - cancel.LastSent >= resendDelay ) && InBudget >= kCancelBytes )
        {
            cancel.LastSent = InNow;
            InRecord.Cancels.push_back( cancel );
            InBudget -= kCancelBytes;
        }
    }
    // Slices while they fit and the share allows (the link bucket owes this packet's bytes so far).
    double link = m_blobLinkTokens - static_cast<double>( InWriter.GetByteCount() );
    BlobSliceRef slice;
    while( InBudget > kSliceOverheadBytes && ( link > 0.0 || m_blobMinTokens > 0.0 ) && TakeSlice( InNow, resendDelay, InBudget - kSliceOverheadBytes, slice ) )
    {
        const OutgoingBlob& blob = *std::find_if( m_blobsOut.begin(), m_blobsOut.end(), [&slice]( const OutgoingBlob& InBlob ) { return InBlob.Id == slice.Blob; } );
        const size_t bytes = SliceBytes( blob.Data.size(), slice.Index );
        InBudget -= bytes + kSliceOverheadBytes;
        link -= static_cast<double>( bytes + kSliceOverheadBytes );
        m_blobMinTokens -= static_cast<double>( bytes + kSliceOverheadBytes );
        InRecord.Slices.push_back( slice );
    }
    m_blobMinTokens = std::max( m_blobMinTokens, -std::max( static_cast<double>( kMaxPacketBytes ), m_blobSettings.MinBytesPerSecond * kBlobBurstSeconds ) );
    if( InRecord.Cancels.empty() && InRecord.Slices.empty() )
    {
        return false;
    }

    InWriter.WriteBool( true );
    InWriter.WriteBool( InExtra );
    for( const BlobCancel& cancel : InRecord.Cancels )
    {
        InWriter.WriteBool( true );
        InWriter.WriteVarUInt( cancel.Id );
        InWriter.WriteBool( cancel.Theirs );
    }
    InWriter.WriteBool( false );
    for( const BlobSliceRef& ref : InRecord.Slices )
    {
        const OutgoingBlob& blob = *std::find_if( m_blobsOut.begin(), m_blobsOut.end(), [&ref]( const OutgoingBlob& InBlob ) { return InBlob.Id == ref.Blob; } );
        const size_t bytes = SliceBytes( blob.Data.size(), ref.Index );
        InWriter.WriteBool( true );
        InWriter.WriteVarUInt( blob.Id );
        InWriter.WriteVarUInt( blob.Data.size() );
        InWriter.WriteVarUInt( ref.Index );
        InWriter.WriteBytes( blob.Data.data() + static_cast<size_t>( ref.Index ) * kBlobSliceBytes, bytes );
        ++m_stats.BlobSlicesSent;
        m_stats.BlobBytesSent += bytes;
    }
    InWriter.WriteBool( false );
    return true;
}


void NetConnection::OnSliceAcked( const BlobSliceRef& InSlice )
{
    auto it = std::find_if( m_blobsOut.begin(), m_blobsOut.end(), [&InSlice]( const OutgoingBlob& InBlob ) { return InBlob.Id == InSlice.Blob; } );
    if( it == m_blobsOut.end() || it->SliceAcked[InSlice.Index] )
    {
        return;
    }
    it->SliceAcked[InSlice.Index] = true;
    if( ++it->AckedCount == it->SliceCount )
    {
        // The remote has it all (and delivered it).
        m_blobsOut.erase( it );
        ++m_stats.BlobsAcked;
    }
}


void NetConnection::MarkIncomingFinished( uint32_t InId )
{
    if( IsIncomingFinished( InId ) )
    {
        return;
    }
    m_blobInFinished.insert( InId );
    while( !m_blobInFinished.empty() && *m_blobInFinished.begin() == m_blobInFloor )
    {
        m_blobInFinished.erase( m_blobInFinished.begin() );
        ++m_blobInFloor;
    }
}


void NetConnection::AcceptSlice( ReceivedSlice&& InSlice, double InNow )
{
    if( IsIncomingFinished( InSlice.Blob ) )
    {
        ++m_stats.BlobSlicesDuplicate;
        return;
    }
    if( InSlice.Blob - m_blobInFloor >= kMaxBlobIdAhead )
    {
        return;
    }
    auto it = std::find_if( m_blobsIn.begin(), m_blobsIn.end(), [&InSlice]( const IncomingBlob& InBlob ) { return InBlob.Id == InSlice.Blob; } );
    if( it == m_blobsIn.end() )
    {
        if( m_blobsIn.size() >= kMaxIncomingBlobs )
        {
            return;
        }
        IncomingBlob blob;
        blob.Id = InSlice.Blob;
        blob.Data.resize( InSlice.Size );
        blob.Have.assign( SliceCount( InSlice.Size ), false );
        blob.StartedAt = InNow;
        m_blobsIn.push_back( std::move( blob ) );
        it = m_blobsIn.end() - 1;
    }
    IncomingBlob& blob = *it;
    if( blob.Data.size() != InSlice.Size )
    {
        return;     // not what its first slice said: not an honest peer's
    }
    if( blob.Have[InSlice.Index] )
    {
        ++m_stats.BlobSlicesDuplicate;
        return;
    }
    std::copy( InSlice.Data.begin(), InSlice.Data.end(), blob.Data.begin() + static_cast<std::ptrdiff_t>( static_cast<size_t>( InSlice.Index ) * kBlobSliceBytes ) );
    blob.Have[InSlice.Index] = true;
    ++blob.HaveCount;
    m_stats.BlobBytesReceived += InSlice.Data.size();
    while( blob.Contiguous < blob.Have.size() && blob.Have[blob.Contiguous] )
    {
        ++blob.Contiguous;
    }
    if( blob.HaveCount == blob.Have.size() )
    {
        const uint32_t id = blob.Id;
        m_blobsDelivered.emplace_back( id, std::move( blob.Data ) );
        m_blobsIn.erase( it );
        MarkIncomingFinished( id );
        ++m_stats.BlobsReceived;
    }
}


void NetConnection::AcceptBlobCancel( uint32_t InId, bool InTheirs )
{
    if( InTheirs )
    {
        // The remote refused one of this end's.
        auto it = std::find_if( m_blobsOut.begin(), m_blobsOut.end(), [InId]( const OutgoingBlob& InBlob ) { return InBlob.Id == InId; } );
        if( it != m_blobsOut.end() )
        {
            m_blobsOut.erase( it );
            ++m_stats.BlobsCancelledByRemote;
        }
        return;
    }
    // The remote stopped one of its own.
    if( IsIncomingFinished( InId ) || InId - m_blobInFloor >= kMaxBlobIdAhead )
    {
        return;
    }
    auto it = std::find_if( m_blobsIn.begin(), m_blobsIn.end(), [InId]( const IncomingBlob& InBlob ) { return InBlob.Id == InId; } );
    if( it != m_blobsIn.end() )
    {
        m_blobsIn.erase( it );
    }
    MarkIncomingFinished( InId );
    ++m_stats.BlobsCancelledByRemote;
}
