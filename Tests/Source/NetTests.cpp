#include "PCH.h"
#include <doctest/doctest.h>
#include "Net/BitStream.h"
#include "Net/NetConnection.h"
#include "Net/NetQuantize.h"
#include "Net/NetTransport.h"
#include "JSON.h"
#include <cmath>
#include <string>
#include <vector>

namespace NetTest
{
    constexpr double kTick = 1.0 / 60.0;

    // Two connections joined through conditioned loopbacks, stepped at the tick rate.
    struct Link
    {
        LoopbackNetwork Network;
        LoopbackTransport RawA{ Network, 1 };
        LoopbackTransport RawB{ Network, 2 };
        ConditionedTransport A;
        ConditionedTransport B;
        NetConnection ToB{ A, 2 };
        NetConnection ToA{ B, 1 };
        double Now = 0.0;
        double Tick = kTick;    // how long each Step is

        Link( NetConditions InAToB, NetConditions InBToA )
            : A( RawA, InAToB )
            , B( RawB, InBToA )
        {
        }

        // One tick: the network delivers at ~1 ms resolution (so arrival times aren't rounded up
        // to the tick, which would read as extra latency), both ends send once at the end.
        void Step()
        {
            constexpr int kSubSteps = 16;
            const double start = Now;
            for( int i = 1; i <= kSubSteps; ++i )
            {
                Now = start + Tick * i / kSubSteps;
                A.Update( Now );
                B.Update( Now );
                NetPacket packet;
                while( A.Receive( packet ) )
                {
                    if( packet.From == 2 )
                    {
                        ToB.ReceivePacket( packet.Data.data(), packet.Data.size(), Now );
                    }
                }
                while( B.Receive( packet ) )
                {
                    if( packet.From == 1 )
                    {
                        ToA.ReceivePacket( packet.Data.data(), packet.Data.size(), Now );
                    }
                }
            }
            ToB.SendPacket( Now );
            ToA.SendPacket( Now );
        }
    };

    std::vector<uint8_t> MessageFor( uint32_t InIndex )
    {
        // 4..300 bytes, the index first so order is checkable, then a pattern.
        std::vector<uint8_t> message( 4 + ( InIndex * 37 ) % 297 );
        for( size_t i = 0; i < message.size(); ++i )
        {
            message[i] = i < 4 ? static_cast<uint8_t>( InIndex >> ( 8 * i ) ) : static_cast<uint8_t>( InIndex * 31 + i );
        }
        return message;
    }
}

using namespace NetTest;

TEST_CASE( "Net: BitStream round-trips bits, varints, floats, bytes and strings, and flags overruns" )
{
    BitWriter writer;
    writer.WriteBool( true );
    writer.WriteBits( 5, 3 );
    writer.WriteBits( 0xDEADBEEF, 32 );
    writer.WriteBits( 0x1234, 13 );
    writer.WriteVarUInt( 0 );
    writer.WriteVarUInt( 127 );
    writer.WriteVarUInt( 128 );
    writer.WriteVarUInt( 0xFFFFFFFFFFFFFFFFull );
    writer.WriteFloat( -3.25f );
    writer.WriteFloat( std::nextafter( 1.f, 2.f ) );
    const uint8_t bytes[] = { 1, 2, 250, 0, 7 };
    writer.WriteBytes( bytes, sizeof( bytes ) );
    writer.WriteString( "h\xC3\xA9llo" );
    writer.WriteBool( false );
    CHECK( writer.GetByteCount() == ( writer.GetBitCount() + 7 ) / 8 );

    BitReader reader( writer.GetBytes() );
    CHECK( reader.ReadBool() );
    CHECK( reader.ReadBits( 3 ) == 5 );
    CHECK( reader.ReadBits( 32 ) == 0xDEADBEEF );
    CHECK( reader.ReadBits( 13 ) == ( 0x1234 & 0x1FFF ) );
    CHECK( reader.ReadVarUInt() == 0 );
    CHECK( reader.ReadVarUInt() == 127 );
    CHECK( reader.ReadVarUInt() == 128 );
    CHECK( reader.ReadVarUInt() == 0xFFFFFFFFFFFFFFFFull );
    CHECK( reader.ReadFloat() == -3.25f );
    CHECK( reader.ReadFloat() == std::nextafter( 1.f, 2.f ) );
    uint8_t read[5] = {};
    CHECK( reader.ReadBytes( read, sizeof( read ) ) );
    CHECK( std::equal( read, read + 5, bytes ) );
    CHECK( reader.ReadString() == "h\xC3\xA9llo" );
    CHECK_FALSE( reader.ReadBool() );
    CHECK_FALSE( reader.IsOverflowed() );

    // Past the end: zero, and the reader says so from then on.
    BitReader truncated( writer.GetBytes().data(), 3 );
    truncated.ReadBits( 20 );
    CHECK( truncated.ReadBits( 8 ) == 0 );
    CHECK( truncated.IsOverflowed() );
    CHECK( truncated.GetBitsRemaining() == 0 );

    // A hostile string length doesn't allocate or read past the data.
    BitWriter hostile;
    hostile.WriteVarUInt( 1u << 30 );
    BitReader hostileReader( hostile.GetBytes() );
    CHECK( hostileReader.ReadString().empty() );
    CHECK( hostileReader.IsOverflowed() );
}

TEST_CASE( "Net: text read off the wire is valid UTF-8 whatever the peer sent" )
{
    // Well-formed text, the edges of each sequence length included, comes back unchanged.
    for( const std::string& good : { std::string( "plain" ), std::string( "h\xC3\xA9llo" ), std::string( "\xE2\x82\xAC 1" ), std::string( "\xF0\x9F\x8E\xAE" ),
             std::string( "\xED\x9F\xBF" ), std::string( "\xEE\x80\x80" ), std::string( "\xF4\x8F\xBF\xBF" ), std::string( "nul\0in", 6 ) } )
    {
        CHECK( ToValidUtf8( good ) == good );
    }
    const std::string bad = "\xEF\xBF\xBD";
    CHECK( ToValidUtf8( "\xFF" ) == bad );                                 // never a lead byte
    CHECK( ToValidUtf8( "a\x80z" ) == "a" + bad + "z" );                  // a stray continuation byte
    CHECK( ToValidUtf8( "\xC0\xAF" ) == bad + bad );                       // an overlong '/'
    CHECK( ToValidUtf8( "\xE0\x80\xAF" ) == bad + bad + bad );             // overlong, three bytes
    CHECK( ToValidUtf8( "\xED\xA0\x80" ) == bad + bad + bad );             // a surrogate
    CHECK( ToValidUtf8( "\xF4\x90\x80\x80" ) == bad + bad + bad + bad );   // past U+10FFFF
    CHECK( ToValidUtf8( "\xE2\x82" ) == bad );                             // cut short: one for the run
    CHECK( ToValidUtf8( "\xE2\x82x" ) == bad + "x" );
    CHECK( ToValidUtf8( "\xF0\x9F\x8E" "a" ) == bad + "a" );
    // nlohmann's dump throws on the raw bytes (what crashed a scripted client), not on the result.
    CHECK_THROWS( json( std::string( "\xFF" ) ).dump() );
    CHECK_NOTHROW( json( ToValidUtf8( "\xFF" ) ).dump() );

    // ReadText: the same over the wire, with ReadString's guards.
    BitWriter writer;
    writer.WriteString( "bye \xFF!" );
    writer.WriteVarUInt( 1u << 30 );
    BitReader reader( writer.GetBytes() );
    CHECK( reader.ReadText() == "bye " + bad + "!" );
    CHECK_FALSE( reader.IsOverflowed() );
    CHECK( reader.ReadText().empty() );
    CHECK( reader.IsOverflowed() );
}

TEST_CASE( "Net: quantized floats, vectors and rotations round-trip within half a step" )
{
    for( const int bits : { 4, 10, 16, 24 } )
    {
        const float step = NetQuantize::Step( -100.f, 100.f, bits );
        for( float value = -100.f; value <= 100.f; value += 3.17f )
        {
            BitWriter writer;
            NetQuantize::WriteFloat( writer, value, -100.f, 100.f, bits );
            CHECK( writer.GetBitCount() == static_cast<size_t>( bits ) );
            BitReader reader( writer.GetBytes() );
            CHECK( std::abs( NetQuantize::ReadFloat( reader, -100.f, 100.f, bits ) - value ) <= step * 0.5f + 1e-4f );
        }
    }
    // The ends are exact; out of range clamps; NaN doesn't escape the range.
    CHECK( NetQuantize::Dequantize( NetQuantize::Quantize( -5.f, -5.f, 5.f, 8 ), -5.f, 5.f, 8 ) == -5.f );
    CHECK( NetQuantize::Dequantize( NetQuantize::Quantize( 5.f, -5.f, 5.f, 8 ), -5.f, 5.f, 8 ) == 5.f );
    CHECK( NetQuantize::Dequantize( NetQuantize::Quantize( 99.f, -5.f, 5.f, 8 ), -5.f, 5.f, 8 ) == 5.f );
    CHECK( NetQuantize::Dequantize( NetQuantize::Quantize( std::nanf( "" ), -5.f, 5.f, 8 ), -5.f, 5.f, 8 ) == -5.f );

    const Vector3 position( 12.345f, -0.5f, 999.f );
    BitWriter vectorWriter;
    NetQuantize::WriteVector3( vectorWriter, position, -1024.f, 1024.f, 20 );
    BitReader vectorReader( vectorWriter.GetBytes() );
    const Vector3 back = NetQuantize::ReadVector3( vectorReader, -1024.f, 1024.f, 20 );
    const float vectorStep = NetQuantize::Step( -1024.f, 1024.f, 20 );
    CHECK( std::abs( back.x - position.x ) <= vectorStep );
    CHECK( std::abs( back.y - position.y ) <= vectorStep );
    CHECK( std::abs( back.z - position.z ) <= vectorStep );

    // Rotations, including ones whose largest component is negative (q and -q are the same).
    uint32_t seed = 7;
    auto next = [&seed]() { seed = seed * 1664525u + 1013904223u; return static_cast<float>( seed >> 8 ) / 16777216.f * 2.f - 1.f; };
    for( int i = 0; i < 500; ++i )
    {
        Quaternion q( next(), next(), next(), next() );
        const float length = std::sqrt( q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w );
        q = Quaternion( q.x / length, q.y / length, q.z / length, q.w / length );
        BitWriter writer;
        NetQuantize::WriteQuaternion( writer, q, 12 );
        CHECK( writer.GetBitCount() == 2 + 3 * 12 );
        BitReader reader( writer.GetBytes() );
        const Quaternion r = NetQuantize::ReadQuaternion( reader, 12 );
        const float dot = std::abs( q.x * r.x + q.y * r.y + q.z * r.z + q.w * r.w );
        CHECK( dot > 0.99999f );   // well under 0.5 degrees
    }
}

TEST_CASE( "Net: the reliable stream arrives once and in order under 20% loss, reordering and duplicates" )
{
    NetConditions bad;
    bad.LatencyMs = 40.f;
    bad.JitterMs = 15.f;
    bad.Loss = 0.2f;
    bad.Duplicate = 0.1f;
    bad.Reorder = 0.1f;
    bad.ReorderMs = 50.f;
    NetConditions back = bad;
    bad.Seed = 11;
    back.Seed = 23;
    Link link( bad, back );

    constexpr uint32_t kForward = 600;
    constexpr uint32_t kBackward = 150;
    uint32_t queuedForward = 0;
    uint32_t queuedBackward = 0;
    std::vector<std::vector<uint8_t>> forward;
    std::vector<std::vector<uint8_t>> backward;
    size_t unreliableSent = 0;
    size_t unreliableReceived = 0;
    for( int tick = 0; tick < 60 * 60; ++tick )
    {
        for( int i = 0; i < 6 && queuedForward < kForward; ++i )
        {
            REQUIRE( link.ToB.SendReliable( MessageFor( queuedForward++ ) ) );
        }
        if( tick % 2 == 0 && queuedBackward < kBackward )
        {
            REQUIRE( link.ToA.SendReliable( MessageFor( 100000 + queuedBackward++ ) ) );
        }
        if( tick < 600 )
        {
            const uint8_t snapshot[16] = { static_cast<uint8_t>( tick ) };
            link.ToB.SendUnreliable( snapshot, sizeof( snapshot ) );
            ++unreliableSent;
        }
        link.Step();
        std::vector<uint8_t> message;
        while( link.ToA.ReceiveReliable( message ) )
        {
            forward.push_back( message );
        }
        while( link.ToB.ReceiveReliable( message ) )
        {
            backward.push_back( message );
        }
        while( link.ToA.ReceiveUnreliable( message ) )
        {
            ++unreliableReceived;
        }
        if( forward.size() == kForward && backward.size() == kBackward && link.ToB.GetPendingReliableCount() == 0 && link.ToA.GetPendingReliableCount() == 0 )
        {
            break;
        }
    }

    REQUIRE( forward.size() == kForward );
    REQUIRE( backward.size() == kBackward );
    for( uint32_t i = 0; i < kForward; ++i )
    {
        REQUIRE( forward[i] == MessageFor( i ) );
    }
    for( uint32_t i = 0; i < kBackward; ++i )
    {
        REQUIRE( backward[i] == MessageFor( 100000 + i ) );
    }
    CHECK( link.ToB.GetPendingReliableCount() == 0 );

    // The link really was that bad, and the connection saw it.
    const NetStats& stats = link.ToB.GetStats();
    CHECK( link.A.GetDroppedCount() > 0 );
    CHECK( link.A.GetDuplicatedCount() > 0 );
    CHECK( link.ToA.GetStats().DuplicatesDropped > 0 );
    CHECK( stats.ReliableResent > 0 );
    CHECK( stats.PacketLoss > 0.1f );
    CHECK( stats.PacketLoss < 0.32f );
    CHECK( stats.PacketsLost > 0 );
    // Unreliable: at most once, most of them.
    CHECK( unreliableReceived <= unreliableSent );
    CHECK( unreliableReceived > unreliableSent * 6 / 10 );
}

TEST_CASE( "Net: the round-trip estimate is within 10% of the link's, and stray packets are rejected" )
{
    for( const float jitter : { 0.f, 10.f } )
    {
        NetConditions link;
        link.LatencyMs = 50.f;
        link.JitterMs = jitter;
        link.Loss = 0.05f;
        link.Seed = 5;
        NetConditions back = link;
        back.Seed = 9;
        Link pair( link, back );
        for( int tick = 0; tick < 60 * 4; ++tick )
        {
            pair.Step();
        }
        const float rtt = pair.ToB.GetStats().RoundTripMs;
        CAPTURE( jitter );
        CAPTURE( rtt );
        CHECK( std::abs( rtt - 100.f ) <= 10.f );
        CHECK( std::abs( pair.ToA.GetStats().RoundTripMs - 100.f ) <= 10.f );
        CHECK( pair.ToB.GetStats().PacketLoss < 0.15f );
    }

    LoopbackNetwork network;
    LoopbackTransport transport( network, 1 );
    NetConnection connection( transport, 2 );
    const uint8_t garbage[] = { 0x12, 0x34, 0x56 };
    CHECK_FALSE( connection.ReceivePacket( garbage, sizeof( garbage ), 0.0 ) );
    // The right protocol id, then a message that claims more bytes than the packet has.
    BitWriter liar;
    liar.WriteBits( NetConnection::kProtocolId, 16 );
    liar.WriteBits( 0, 16 );
    liar.WriteBool( false );
    liar.WriteBool( true );
    liar.WriteBool( true );
    liar.WriteBits( 0, 16 );
    liar.WriteVarUInt( 900 );
    CHECK_FALSE( connection.ReceivePacket( liar.GetBytes().data(), liar.GetByteCount(), 0.0 ) );
    CHECK( connection.GetStats().PacketsRejected == 2 );
    std::vector<uint8_t> nothing;
    CHECK_FALSE( connection.ReceiveReliable( nothing ) );
    // Oversized messages are refused up front.
    std::vector<uint8_t> huge( NetConnection::kMaxMessageBytes + 1 );
    CHECK_FALSE( connection.SendReliable( huge ) );
}


TEST_CASE( "Net: one end conditioning what it sends and what it receives makes the whole round trip (the editor's listen server, NET-7)" )
{
    // Only A is conditioned, both ways: 50 ms each way is 100 ms of RTT, as 50 on each end would be.
    NetConditions link;
    link.LatencyMs = 50.f;
    link.Loss = 0.05f;
    link.Seed = 5;
    Link pair( link, NetConditions() );
    NetConditions inbound = link;
    inbound.Seed = 9;
    pair.A.SetInboundConditions( inbound );
    for( int tick = 0; tick < 60 * 4; ++tick )
    {
        pair.Step();
    }
    CHECK( std::abs( pair.ToB.GetStats().RoundTripMs - 100.f ) <= 10.f );
    CHECK( std::abs( pair.ToA.GetStats().RoundTripMs - 100.f ) <= 10.f );
    CHECK( pair.A.GetDroppedCount() > 0 );
    CHECK( pair.A.GetInboundDroppedCount() > 0 );
    CHECK( pair.B.GetDroppedCount() == 0 );

    // A live change (a slider) takes effect for what arrives next, without restarting the link.
    NetConditions slower = inbound;
    slower.LatencyMs = 150.f;
    slower.Loss = 0.f;
    pair.A.SetInboundConditions( slower, false );
    for( int tick = 0; tick < 60 * 4; ++tick )
    {
        pair.Step();
    }
    CHECK( std::abs( pair.ToB.GetStats().RoundTripMs - 200.f ) <= 20.f );

    // Arrivals alone: each is held its latency from when it came in, in arrival order; losses and
    // duplicates count; turning it off hands out what's held at once, then passes straight through.
    LoopbackNetwork network;
    LoopbackTransport rawA( network, 1 );
    LoopbackTransport rawB( network, 2 );
    ConditionedTransport a( rawA, NetConditions() );
    NetConditions arrive;
    arrive.LatencyMs = 40.f;
    arrive.Seed = 3;
    a.SetInboundConditions( arrive );
    a.Update( 1.0 );
    for( uint8_t i = 0; i < 10; ++i )
    {
        rawB.Send( 1, &i, 1 );
    }
    NetPacket packet;
    CHECK_FALSE( a.Receive( packet ) );
    CHECK( a.GetHeldCount() == 10 );
    a.Update( 1.039 );
    CHECK_FALSE( a.Receive( packet ) );
    a.Update( 1.041 );
    for( uint8_t i = 0; i < 10; ++i )
    {
        REQUIRE( a.Receive( packet ) );
        CHECK( packet.From == 2 );
        REQUIRE( packet.Data.size() == 1 );
        CHECK( packet.Data[0] == i );
    }
    CHECK_FALSE( a.Receive( packet ) );
    for( uint8_t i = 0; i < 4; ++i )
    {
        rawB.Send( 1, &i, 1 );
    }
    CHECK_FALSE( a.Receive( packet ) );
    a.SetInboundConditions( NetConditions() );
    int handed = 0;
    while( a.Receive( packet ) )
    {
        CHECK( packet.Data[0] == handed );
        ++handed;
    }
    CHECK( handed == 4 );
    const uint8_t straight = 7;
    rawB.Send( 1, &straight, 1 );
    REQUIRE( a.Receive( packet ) );
    CHECK( packet.Data[0] == 7 );

    NetConditions lossy;
    lossy.Loss = 0.5f;
    lossy.Duplicate = 0.5f;
    lossy.Seed = 11;
    a.SetInboundConditions( lossy );
    for( int i = 0; i < 200; ++i )
    {
        const uint8_t byte = static_cast<uint8_t>( i );
        rawB.Send( 1, &byte, 1 );
    }
    int received = 0;
    while( a.Receive( packet ) )
    {
        ++received;
    }
    CHECK( a.GetInboundDroppedCount() > 60 );
    CHECK( a.GetInboundDuplicatedCount() > 20 );
    CHECK( received == 200 - static_cast<int>( a.GetInboundDroppedCount() ) + static_cast<int>( a.GetInboundDuplicatedCount() ) );
}


TEST_CASE( "Net: a packet is the newest by its sequence however long the link idled, and a reordered one isn't" )
{
    // Every packet, a keep-alive too, moves the received sequence on, so "newest" never wraps:
    // after 40000 packets (past half the 16-bit range; 22 minutes of a paused client's keep-alives)
    // the next one is still the newest.
    LoopbackNetwork network;
    LoopbackTransport rawA( network, 1 );
    LoopbackTransport rawB( network, 2 );
    NetConnection a( rawA, 2 );
    NetConnection b( rawB, 1 );
    double now = 0.0;
    size_t notNewest = 0;
    NetPacket packet;
    for( int i = 0; i < 40000; ++i )
    {
        a.SendPacket( now );
        now += 1.0 / 30.0;
        while( rawB.Receive( packet ) )
        {
            REQUIRE( b.ReceivePacket( packet.Data.data(), packet.Data.size(), now ) );
            notNewest += b.WasLastPacketNewest() ? 0 : 1;
        }
    }
    CHECK( notNewest == 0 );
    // Two packets, the later one first: the earlier is not the newest.
    a.SendPacket( now );
    NetPacket first;
    REQUIRE( rawB.Receive( first ) );
    a.SendPacket( now );
    REQUIRE( rawB.Receive( packet ) );
    REQUIRE( b.ReceivePacket( packet.Data.data(), packet.Data.size(), now ) );
    CHECK( b.WasLastPacketNewest() );
    REQUIRE( b.ReceivePacket( first.Data.data(), first.Data.size(), now ) );
    CHECK_FALSE( b.WasLastPacketNewest() );
    // The ack it carried: what the far end had of this end's packets.
    uint16_t ack = 0;
    CHECK_FALSE( b.GetLastPacketAck( ack ) );
    b.SendPacket( now );
    REQUIRE( rawA.Receive( packet ) );
    REQUIRE( a.ReceivePacket( packet.Data.data(), packet.Data.size(), now ) );
    REQUIRE( a.GetLastPacketAck( ack ) );
    CHECK( ack == static_cast<uint16_t>( a.GetNextSequence() - 1 ) );
}


TEST_CASE( "Net: an end that sends seldom (5 fps) keeps sampling the round trip, so its deviation settles" )
{
    // A packet read long after this end last sent waited on it (a hitch), so it gives no sample;
    // "long" is three of this end's usual gaps, at least 100 ms. With a fixed 100 ms an end at
    // 5 fps never sampled again after its first, which set the deviation to half the round trip.
    NetConditions link;
    link.LatencyMs = 50.f;
    NetTest::Link pair( link, link );
    for( int tick = 0; tick < 60 * 20; ++tick )
    {
        // B sends every tick (Step sends both; A's packet only goes out every 12th tick).
        constexpr int kSubSteps = 16;
        const double start = pair.Now;
        for( int i = 1; i <= kSubSteps; ++i )
        {
            pair.Now = start + NetTest::kTick * i / kSubSteps;
            pair.A.Update( pair.Now );
            pair.B.Update( pair.Now );
            NetPacket packet;
            while( pair.B.Receive( packet ) )
            {
                pair.ToA.ReceivePacket( packet.Data.data(), packet.Data.size(), pair.Now );
            }
        }
        // A is a 5 fps client: it reads and sends once a frame.
        if( tick % 12 == 0 )
        {
            NetPacket packet;
            while( pair.A.Receive( packet ) )
            {
                pair.ToB.ReceivePacket( packet.Data.data(), packet.Data.size(), pair.Now );
            }
            pair.ToB.SendPacket( pair.Now );
        }
        pair.ToA.SendPacket( pair.Now );
    }
    const NetStats& slow = pair.ToB.GetStats();
    MESSAGE( "5 fps end: round trip " << slow.RoundTripMs << " ms, deviation " << slow.RoundTripDeviationMs << " ms" );
    CHECK( slow.RoundTripMs > 90.f );
    CHECK( slow.RoundTripDeviationMs < 0.2f * slow.RoundTripMs );
}


// Blobs (ME-323) ---------------------------------------------------------------------------------

namespace NetTest
{
    // InSize bytes with the seed folded in at every position, so a misplaced slice shows.
    std::vector<uint8_t> BlobFor( size_t InSize, uint32_t InSeed )
    {
        std::vector<uint8_t> blob( InSize );
        uint32_t state = InSeed * 2654435761u + 1;
        for( size_t i = 0; i < InSize; ++i )
        {
            state = state * 1664525u + 1013904223u;
            blob[i] = static_cast<uint8_t>( ( state >> 24 ) ^ ( i >> 10 ) );
        }
        return blob;
    }

    struct BlobInbox
    {
        std::vector<std::pair<uint32_t, std::vector<uint8_t>>> Blobs;

        void Drain( NetConnection& InConnection )
        {
            uint32_t id = 0;
            std::vector<uint8_t> data;
            while( InConnection.ReceiveBlob( id, data ) )
            {
                Blobs.emplace_back( id, std::move( data ) );
            }
        }
        const std::vector<uint8_t>* Find( uint32_t InId ) const
        {
            for( const auto& [id, data] : Blobs )
            {
                if( id == InId )
                {
                    return &data;
                }
            }
            return nullptr;
        }
    };

    const NetBlobProgress* FindProgress( const std::vector<NetBlobProgress>& InList, uint32_t InId, bool InOutgoing )
    {
        for( const NetBlobProgress& progress : InList )
        {
            if( progress.Id == InId && progress.Outgoing == InOutgoing )
            {
                return &progress;
            }
        }
        return nullptr;
    }
}


TEST_CASE( "Net: blobs arrive whole and in order on a clean link, four at a time; empty and past-4 MB ones are refused" )
{
    NetTest::Link link( NetConditions{}, NetConditions{} );
    NetBlobSettings fast;
    fast.LinkBytesPerSecond = 1024 * 1024;
    link.ToB.SetBlobSettings( fast );

    // The size cap: nothing queued past it, nor for nothing at all.
    CHECK( link.ToB.SendBlob( nullptr, 0 ) == 0 );
    std::vector<uint8_t> tooBig( NetConnection::kMaxBlobBytes + 1, 7 );
    CHECK( link.ToB.SendBlob( tooBig ) == 0 );
    CHECK( link.ToB.GetStats().BlobsRefused == 2 );
    CHECK( link.ToB.GetPendingBlobCount() == 0 );
    tooBig.clear();

    // Slice edges (one byte, exactly one slice, one past it), a few hundred KB, and the cap itself.
    const std::vector<size_t> sizes = { 1, NetConnection::kBlobSliceBytes, NetConnection::kBlobSliceBytes + 1, 3000, 200 * 1024 + 7, NetConnection::kMaxBlobBytes };
    std::vector<uint32_t> ids;
    for( size_t i = 0; i < sizes.size(); ++i )
    {
        const uint32_t id = link.ToB.SendBlob( NetTest::BlobFor( sizes[i], static_cast<uint32_t>( i ) ) );
        REQUIRE( id != 0 );
        ids.push_back( id );
    }
    // Four go at once; the rest queue.
    std::vector<NetBlobProgress> progress = link.ToB.GetBlobProgress();
    REQUIRE( progress.size() == sizes.size() );
    CHECK( progress[3].Started );
    CHECK_FALSE( progress[4].Started );

    NetTest::BlobInbox inbox;
    bool sawPartial = false;
    for( int tick = 0; tick < 60 * 20 && inbox.Blobs.size() < sizes.size(); ++tick )
    {
        link.Step();
        inbox.Drain( link.ToA );
        // Partway: the receiver shows what it has, from the start without a gap on a clean link.
        const std::vector<NetBlobProgress> arriving = link.ToA.GetBlobProgress();
        if( const NetBlobProgress* incoming = NetTest::FindProgress( arriving, ids.back(), false ) )
        {
            if( incoming->DoneBytes > 0 && incoming->DoneBytes < incoming->Size )
            {
                sawPartial = true;
                CHECK( incoming->ContiguousBytes == incoming->DoneBytes );
                CHECK( incoming->Size == NetConnection::kMaxBlobBytes );
            }
        }
    }
    REQUIRE( inbox.Blobs.size() == sizes.size() );
    CHECK( sawPartial );
    for( size_t i = 0; i < sizes.size(); ++i )
    {
        const std::vector<uint8_t>* data = inbox.Find( ids[i] );
        REQUIRE( data );
        CHECK( *data == NetTest::BlobFor( sizes[i], static_cast<uint32_t>( i ) ) );
    }
    // Small ones aren't held behind big ones.
    CHECK( inbox.Blobs.back().first == ids.back() );
    for( int tick = 0; tick < 30; ++tick )
    {
        link.Step();
    }
    CHECK( link.ToB.GetPendingBlobCount() == 0 );
    CHECK( link.ToB.GetBlobProgress().empty() );
    CHECK( link.ToA.GetBlobProgress().empty() );
    const NetStats& sent = link.ToB.GetStats();
    CHECK( sent.BlobsSent == sizes.size() );
    CHECK( sent.BlobsAcked == sizes.size() );
    CHECK( link.ToA.GetStats().BlobsReceived == sizes.size() );
    CHECK( sent.BlobSlicesResent == 0 );
    CHECK( sent.BlobPacketsSent > 0 );
}


TEST_CASE( "Net: two concurrent blobs each way reassemble under 20% loss, reordering and duplicates, beside the reliable stream" )
{
    NetConditions bad;
    bad.LatencyMs = 40.f;
    bad.JitterMs = 15.f;
    bad.Loss = 0.2f;
    bad.Duplicate = 0.1f;
    bad.Reorder = 0.1f;
    bad.ReorderMs = 50.f;
    NetConditions back = bad;
    bad.Seed = 31;
    back.Seed = 37;
    NetTest::Link link( bad, back );

    const std::vector<uint8_t> big = NetTest::BlobFor( 300 * 1024 + 11, 1 );
    const std::vector<uint8_t> small = NetTest::BlobFor( 77 * 1024, 2 );
    const std::vector<uint8_t> up1 = NetTest::BlobFor( 50 * 1024 + 3, 3 );
    const std::vector<uint8_t> up2 = NetTest::BlobFor( 9000, 4 );
    const uint32_t bigId = link.ToB.SendBlob( big );
    const uint32_t smallId = link.ToB.SendBlob( small );
    const uint32_t up1Id = link.ToA.SendBlob( up1 );
    const uint32_t up2Id = link.ToA.SendBlob( up2 );
    REQUIRE( ( bigId && smallId && up1Id && up2Id ) );

    NetTest::BlobInbox atA;
    NetTest::BlobInbox atB;
    std::vector<std::vector<uint8_t>> reliable;
    uint32_t queued = 0;
    size_t unreliableSent = 0;
    size_t unreliableReceived = 0;
    size_t bigDoneWhenSmallArrived = 0;
    for( int tick = 0; tick < 60 * 60; ++tick )
    {
        if( tick % 3 == 0 && queued < 200 )
        {
            REQUIRE( link.ToB.SendReliable( MessageFor( queued++ ) ) );
        }
        const uint8_t snapshot[400] = { static_cast<uint8_t>( tick ) };
        link.ToB.SendUnreliable( snapshot, sizeof( snapshot ) );
        ++unreliableSent;
        link.Step();
        const size_t before = atA.Blobs.size();
        atA.Drain( link.ToA );
        atB.Drain( link.ToB );
        if( before == 0 && atA.Find( smallId ) )
        {
            const std::vector<NetBlobProgress> arriving = link.ToA.GetBlobProgress();
            const NetBlobProgress* progress = NetTest::FindProgress( arriving, bigId, false );
            bigDoneWhenSmallArrived = progress ? progress->DoneBytes : 0;
        }
        std::vector<uint8_t> message;
        while( link.ToA.ReceiveReliable( message ) )
        {
            reliable.push_back( message );
        }
        while( link.ToA.ReceiveUnreliable( message ) )
        {
            ++unreliableReceived;
        }
        if( atA.Blobs.size() == 2 && atB.Blobs.size() == 2 && reliable.size() == 200 && link.ToB.GetPendingBlobCount() == 0 && link.ToA.GetPendingBlobCount() == 0 )
        {
            break;
        }
    }

    REQUIRE( atA.Blobs.size() == 2 );
    REQUIRE( atB.Blobs.size() == 2 );
    CHECK( *atA.Find( bigId ) == big );
    CHECK( *atA.Find( smallId ) == small );
    CHECK( *atB.Find( up1Id ) == up1 );
    CHECK( *atB.Find( up2Id ) == up2 );
    // Concurrent, taking turns: the big one was well under way when the small one completed.
    CHECK( bigDoneWhenSmallArrived > 50 * 1024 );
    // The reliable stream beside them: still exactly once and in order.
    REQUIRE( reliable.size() == 200 );
    for( uint32_t i = 0; i < 200; ++i )
    {
        REQUIRE( reliable[i] == MessageFor( i ) );
    }
    CHECK( link.ToB.GetPendingBlobCount() == 0 );
    CHECK( link.ToA.GetPendingBlobCount() == 0 );
    // The link really was that bad, and the channel coped.
    const NetStats& down = link.ToB.GetStats();
    CHECK( down.BlobSlicesResent > 0 );
    CHECK( link.ToA.GetStats().BlobSlicesDuplicate > 0 );
    CHECK( link.A.GetDuplicatedCount() > 0 );
    CHECK( down.BlobsAcked == 2 );
    CHECK( link.ToA.GetStats().BlobsAcked == 2 );
    CHECK( unreliableReceived > unreliableSent * 6 / 10 );
    MESSAGE( "down: " << down.BlobSlicesSent << " slices sent, " << down.BlobSlicesResent << " resent, " << down.BlobPacketsSent << " slice packets" );
}


TEST_CASE( "Net: a blob cancelled by its sender, or refused by its receiver, stops; the next one still arrives" )
{
    NetConditions lossy;
    lossy.LatencyMs = 30.f;
    lossy.Loss = 0.1f;
    NetConditions back = lossy;
    lossy.Seed = 5;
    back.Seed = 6;
    NetTest::Link link( lossy, back );

    // The sender cancels partway.
    const uint32_t dropped = link.ToB.SendBlob( NetTest::BlobFor( 400 * 1024, 10 ) );
    const uint32_t next = link.ToB.SendBlob( NetTest::BlobFor( 20 * 1024, 11 ) );
    NetTest::BlobInbox inbox;
    for( int tick = 0; tick < 600; ++tick )
    {
        link.Step();
        inbox.Drain( link.ToA );
        const std::vector<NetBlobProgress> arriving = link.ToA.GetBlobProgress();
        const NetBlobProgress* progress = NetTest::FindProgress( arriving, dropped, false );
        if( progress && progress->DoneBytes > 100 * 1024 )
        {
            break;
        }
    }
    REQUIRE( NetTest::FindProgress( link.ToA.GetBlobProgress(), dropped, false ) );
    CHECK( link.ToB.CancelBlob( dropped ) );
    CHECK_FALSE( link.ToB.CancelBlob( dropped ) );
    for( int tick = 0; tick < 60 * 5; ++tick )
    {
        link.Step();
        inbox.Drain( link.ToA );
    }
    CHECK( inbox.Find( dropped ) == nullptr );
    REQUIRE( inbox.Find( next ) );
    CHECK( *inbox.Find( next ) == NetTest::BlobFor( 20 * 1024, 11 ) );
    CHECK( link.ToA.GetBlobProgress().empty() );
    CHECK( link.ToB.GetPendingBlobCount() == 0 );
    CHECK( link.ToB.GetStats().BlobsCancelled == 1 );
    CHECK( link.ToA.GetStats().BlobsCancelledByRemote == 1 );
    CHECK( link.ToA.GetStats().BlobsReceived == 1 );

    // The receiver refuses one partway (a skipped killcam): the sender stops sending it.
    const uint32_t refused = link.ToB.SendBlob( NetTest::BlobFor( 600 * 1024, 12 ) );
    for( int tick = 0; tick < 600; ++tick )
    {
        link.Step();
        const std::vector<NetBlobProgress> arriving = link.ToA.GetBlobProgress();
        const NetBlobProgress* progress = NetTest::FindProgress( arriving, refused, false );
        if( progress && progress->DoneBytes > 50 * 1024 )
        {
            break;
        }
    }
    CHECK( link.ToA.CancelIncomingBlob( refused ) );
    CHECK_FALSE( link.ToA.CancelIncomingBlob( refused ) );
    for( int tick = 0; tick < 60; ++tick )
    {
        link.Step();
        inbox.Drain( link.ToA );
    }
    CHECK( link.ToB.GetPendingBlobCount() == 0 );
    CHECK( link.ToB.GetStats().BlobsCancelledByRemote == 1 );
    const uint64_t slicesThen = link.ToB.GetStats().BlobSlicesSent;
    for( int tick = 0; tick < 60 * 2; ++tick )
    {
        link.Step();
        inbox.Drain( link.ToA );
    }
    CHECK( link.ToB.GetStats().BlobSlicesSent == slicesThen );
    CHECK( inbox.Find( refused ) == nullptr );
    CHECK( link.ToA.GetBlobProgress().empty() );
    CHECK( link.ToA.GetStats().BlobsCancelled == 1 );

    // And the channel still works after both.
    const uint32_t after = link.ToB.SendBlob( NetTest::BlobFor( 5000, 13 ) );
    for( int tick = 0; tick < 120 && !inbox.Find( after ); ++tick )
    {
        link.Step();
        inbox.Drain( link.ToA );
    }
    REQUIRE( inbox.Find( after ) );
    CHECK( *inbox.Find( after ) == NetTest::BlobFor( 5000, 13 ) );
}


TEST_CASE( "Net: blobs get what live traffic leaves of the link, and at least their minimum share; live messages are never displaced" )
{
    auto measure = [&]( size_t InLiveBytes, const NetBlobSettings& InSettings, double& OutBlobRate, double& OutLinkRate, size_t& OutLiveSent, size_t& OutLiveReceived ) {
        NetTest::Link link( NetConditions{}, NetConditions{} );
        link.ToB.SetBlobSettings( InSettings );
        REQUIRE( link.ToB.SendBlob( NetTest::BlobFor( 2 * 1024 * 1024, 20 ) ) );
        std::vector<uint8_t> live( InLiveBytes, 0x5A );
        OutLiveSent = 0;
        OutLiveReceived = 0;
        uint64_t blobBytes0 = 0;
        uint64_t linkBytes0 = 0;
        for( int tick = 0; tick < 60 * 4; ++tick )
        {
            if( tick == 60 )
            {
                blobBytes0 = link.ToA.GetStats().BlobBytesReceived;
                linkBytes0 = link.ToB.GetStats().BytesSent;
            }
            REQUIRE( link.ToB.SendUnreliable( live ) );
            ++OutLiveSent;
            link.Step();
            std::vector<uint8_t> message;
            while( link.ToA.ReceiveUnreliable( message ) )
            {
                OutLiveReceived += message.size() == InLiveBytes ? 1 : 0;
            }
        }
        OutBlobRate = static_cast<double>( link.ToA.GetStats().BlobBytesReceived - blobBytes0 ) / 3.0;
        OutLinkRate = static_cast<double>( link.ToB.GetStats().BytesSent - linkBytes0 ) / 3.0;
        // The last tick's packet arrives in the next step.
        link.Step();
        std::vector<uint8_t> message;
        while( link.ToA.ReceiveUnreliable( message ) )
        {
            OutLiveReceived += message.size() == InLiveBytes ? 1 : 0;
        }
        REQUIRE( link.ToB.GetPendingBlobCount() == 1 );     // still going: the whole window measured it
    };

    NetBlobSettings settings;
    settings.LinkBytesPerSecond = 64 * 1024;
    settings.MinBytesPerSecond = 8 * 1024;
    double blobRate = 0.0;
    double linkRate = 0.0;
    size_t liveSent = 0;
    size_t liveReceived = 0;

    // Light live traffic (about 13 KB/s): blobs fill the link up to its budget, not past it.
    measure( 200, settings, blobRate, linkRate, liveSent, liveReceived );
    MESSAGE( "light: blobs " << blobRate << " B/s, link " << linkRate << " B/s" );
    CHECK( liveReceived == liveSent );
    CHECK( linkRate <= settings.LinkBytesPerSecond * 1.03 );
    CHECK( linkRate >= settings.LinkBytesPerSecond * 0.9 );
    CHECK( blobRate > settings.LinkBytesPerSecond * 0.7 );

    // Live traffic over the whole budget (about 61 KB/s against 32): blobs still get their minimum share, and live loses nothing.
    settings.LinkBytesPerSecond = 32 * 1024;
    measure( 1000, settings, blobRate, linkRate, liveSent, liveReceived );
    MESSAGE( "heavy: blobs " << blobRate << " B/s, link " << linkRate << " B/s" );
    CHECK( liveReceived == liveSent );
    CHECK( blobRate >= settings.MinBytesPerSecond * 0.85 );
    CHECK( blobRate <= settings.MinBytesPerSecond * 1.25 );
}


TEST_CASE( "Net: an end at 20 or 30 Hz still spends its whole blob share" )
{
    // The buckets hold two of the end's usual gaps between packets: with a fixed 35 ms a 20 Hz end
    // could spend 70% of its link budget at most.
    for( const double hz : { 20.0, 30.0 } )
    {
        NetTest::Link link( NetConditions{}, NetConditions{} );
        link.Tick = 1.0 / hz;
        NetBlobSettings settings;
        link.ToB.SetBlobSettings( settings );
        REQUIRE( link.ToB.SendBlob( NetTest::BlobFor( 2 * 1024 * 1024, 21 ) ) );
        uint64_t linkBytes0 = 0;
        uint64_t blobBytes0 = 0;
        const int ticks = static_cast<int>( hz * 4.0 );
        for( int tick = 0; tick < ticks; ++tick )
        {
            if( tick == static_cast<int>( hz ) )
            {
                linkBytes0 = link.ToB.GetStats().BytesSent;
                blobBytes0 = link.ToA.GetStats().BlobBytesReceived;
            }
            link.Step();
        }
        const double linkRate = static_cast<double>( link.ToB.GetStats().BytesSent - linkBytes0 ) / 3.0;
        const double blobRate = static_cast<double>( link.ToA.GetStats().BlobBytesReceived - blobBytes0 ) / 3.0;
        MESSAGE( hz << " Hz: blobs " << blobRate << " B/s, link " << linkRate << " B/s, " << link.ToB.GetStats().BlobSlicesResent << " slices resent" );
        CHECK( linkRate >= settings.LinkBytesPerSecond * 0.9 );
        // And spent on new slices: the far end acks a tick late, which a resend mustn't beat.
        CHECK( blobRate >= settings.LinkBytesPerSecond * 0.85 );
        CHECK( link.ToB.GetStats().BlobSlicesResent == 0 );
        CHECK( linkRate <= settings.LinkBytesPerSecond * 1.03 );
        CHECK( link.ToB.GetPendingBlobCount() == 1 );
    }
}


TEST_CASE( "Net: a packet of blob slices alone is never the newest, and an ack of one doesn't stand for the packet before it" )
{
    LoopbackNetwork network;
    LoopbackTransport rawA( network, 1 );
    LoopbackTransport rawB( network, 2 );
    NetConnection a( rawA, 2 );
    NetConnection b( rawB, 1 );
    double now = 0.0;
    NetPacket packet;
    auto exchange = [&]() {
        a.SendPacket( now );
        b.SendPacket( now );
        while( rawB.Receive( packet ) )
        {
            b.ReceivePacket( packet.Data.data(), packet.Data.size(), now );
        }
        while( rawA.Receive( packet ) )
        {
            a.ReceivePacket( packet.Data.data(), packet.Data.size(), now );
        }
        now += 1.0 / 60.0;
    };
    for( int i = 0; i < 10; ++i )
    {
        exchange();
    }
    // A blob, and a live message that fills the packet SendPacket always sends: the slices go in
    // packets of their own right after it.
    REQUIRE( a.SendBlob( NetTest::BlobFor( 8000, 30 ) ) );
    const std::vector<uint8_t> live( 1000, 0x11 );
    auto sendRound = [&]( std::vector<NetPacket>& OutSent ) {
        now += 0.05;
        REQUIRE( a.SendUnreliable( live ) );
        a.SendPacket( now );
        OutSent.clear();
        while( rawB.Receive( packet ) )
        {
            OutSent.push_back( packet );
        }
    };
    std::vector<NetPacket> sent;
    const uint16_t main = a.GetNextSequence();
    sendRound( sent );
    REQUIRE( sent.size() >= 2 );
    // The slice packet arrives first: it isn't the newest, and the main packet after it still is.
    REQUIRE( b.ReceivePacket( sent[1].Data.data(), sent[1].Data.size(), now ) );
    CHECK_FALSE( b.WasLastPacketNewest() );
    REQUIRE( b.ReceivePacket( sent[0].Data.data(), sent[0].Data.size(), now ) );
    CHECK( b.WasLastPacketNewest() );
    std::vector<uint8_t> message;
    CHECK( b.ReceiveUnreliable( message ) );
    CHECK( message == live );

    // The next main packet is lost and a slice packet behind it arrives: what B tells A it has
    // acks that slice packet, yet A reads the newest of its main packets B has: the one before.
    const uint16_t lost = a.GetNextSequence();
    sendRound( sent );
    REQUIRE( sent.size() >= 2 );
    REQUIRE( b.ReceivePacket( sent[1].Data.data(), sent[1].Data.size(), now ) );
    CHECK_FALSE( b.WasLastPacketNewest() );
    b.SendPacket( now );
    REQUIRE( rawA.Receive( packet ) );
    REQUIRE( a.ReceivePacket( packet.Data.data(), packet.Data.size(), now ) );
    uint16_t ack = 0;
    REQUIRE( a.GetLastPacketAck( ack ) );
    CHECK( ack == main );
    CHECK( NetSequenceGreater( lost, ack ) );
}


TEST_CASE( "Net: forged blob trailers reserve nothing where blobs aren't allowed, and a hostile peer's ids, counts, sizes and indexes are refused" )
{
    LoopbackNetwork network;
    LoopbackTransport raw( network, 1 );
    uint16_t sequence = 0;
    auto header = [&]( BitWriter& InWriter ) {
        InWriter.WriteBits( NetConnection::kProtocolId, 16 );
        InWriter.WriteBits( sequence++, 16 );
        InWriter.WriteBool( false );    // no ack
    };
    // A packet of one slice (alone): blob InId of InSize bytes, slice InIndex.
    auto slicePacket = [&]( uint32_t InId, uint32_t InSize, uint32_t InIndex ) {
        BitWriter writer;
        header( writer );
        writer.WriteBool( false );      // no messages
        writer.WriteBool( true );       // the trailer
        writer.WriteBool( true );       // slices alone
        writer.WriteBool( false );      // no cancels
        writer.WriteBool( true );
        writer.WriteVarUInt( InId );
        writer.WriteVarUInt( InSize );
        writer.WriteVarUInt( InIndex );
        const size_t first = static_cast<size_t>( InIndex ) * NetConnection::kBlobSliceBytes;
        const std::vector<uint8_t> bytes( first < InSize ? std::min( NetConnection::kBlobSliceBytes, InSize - first ) : 0, 0x42 );
        writer.WriteBytes( bytes.data(), bytes.size() );
        writer.WriteBool( false );
        return writer.GetBytes();
    };
    auto cancelPacket = [&]( uint32_t InFirst, uint32_t InCount ) {
        BitWriter writer;
        header( writer );
        writer.WriteBool( false );
        writer.WriteBool( true );
        writer.WriteBool( false );
        for( uint32_t i = 0; i < InCount; ++i )
        {
            writer.WriteBool( true );
            writer.WriteVarUInt( InFirst + i );
            writer.WriteBool( false );  // the sender's own blob
        }
        writer.WriteBool( false );
        writer.WriteBool( false );      // no slices
        return writer.GetBytes();
    };
    auto receive = [&]( NetConnection& InConnection, const std::vector<uint8_t>& InPacket ) {
        return InConnection.ReceivePacket( InPacket.data(), InPacket.size(), 0.0 );
    };

    // Not allowed (a server's peer before it's accepted): a slice claiming 4 MB reserves nothing.
    {
        NetConnection closed( raw, 2 );
        closed.SetAcceptIncomingBlobs( false );
        CHECK( receive( closed, slicePacket( 1, NetConnection::kMaxBlobBytes, 0 ) ) );
        CHECK( receive( closed, cancelPacket( 2, 10 ) ) );
        CHECK( closed.GetBlobProgress().empty() );
        CHECK( closed.GetStats().BlobSlicesIgnored == 1 );
        CHECK( closed.GetStats().BlobsCancelledByRemote == 0 );
        CHECK( closed.GetStats().PacketsRejected == 0 );
        // Allowed later, the next slice is taken in.
        closed.SetAcceptIncomingBlobs( true );
        CHECK( receive( closed, slicePacket( 1, 3000, 0 ) ) );
        CHECK( closed.GetBlobProgress().size() == 1 );
    }

    NetConnection connection( raw, 2 );
    // An id far past the oldest one not done.
    CHECK( receive( connection, slicePacket( 1 + ( 1u << 20 ), 3000, 0 ) ) );
    CHECK( connection.GetBlobProgress().empty() );
    CHECK( connection.GetStats().BlobSlicesIgnored == 1 );
    // A blob whose size changes between its slices: the later one is dropped.
    CHECK( receive( connection, slicePacket( 1, 5000, 0 ) ) );
    CHECK( receive( connection, slicePacket( 1, 6000, 1 ) ) );
    std::vector<NetBlobProgress> progress = connection.GetBlobProgress();
    REQUIRE( progress.size() == 1 );
    CHECK( progress[0].Size == 5000 );
    CHECK( progress[0].DoneBytes == NetConnection::kBlobSliceBytes );
    CHECK( connection.GetStats().BlobSlicesIgnored == 2 );
    // An index past the blob's slices: the packet is malformed, rejected whole.
    CHECK_FALSE( receive( connection, slicePacket( 2, 5000, 5 ) ) );
    CHECK( connection.GetStats().PacketsRejected == 1 );
    // Eight blobs at once at most; a ninth is dropped.
    for( uint32_t id = 2; id <= NetConnection::kMaxIncomingBlobs; ++id )
    {
        CHECK( receive( connection, slicePacket( id, 5000, 0 ) ) );
    }
    CHECK( connection.GetBlobProgress().size() == NetConnection::kMaxIncomingBlobs );
    CHECK( receive( connection, slicePacket( NetConnection::kMaxIncomingBlobs + 1, 5000, 0 ) ) );
    CHECK( connection.GetBlobProgress().size() == NetConnection::kMaxIncomingBlobs );
    CHECK( connection.GetStats().BlobSlicesIgnored == 3 );

    // Cancels for ids never seen are held (late slices of them are old news) up to a bound: a
    // peer cancelling ids by the million can't grow the set without end.
    const uint32_t first = NetConnection::kMaxIncomingBlobs + 1;
    const uint32_t count = static_cast<uint32_t>( NetConnection::kMaxIncomingFinished ) + 200;
    for( uint32_t done = 0; done < count; done += 100 )
    {
        REQUIRE( receive( connection, cancelPacket( first + done, std::min<uint32_t>( 100, count - done ) ) ) );
    }
    CHECK( connection.GetStats().BlobsCancelledByRemote == NetConnection::kMaxIncomingFinished );
    const uint64_t duplicates = connection.GetStats().BlobSlicesDuplicate;
    CHECK( receive( connection, slicePacket( first, 5000, 0 ) ) );     // cancelled: old news
    CHECK( connection.GetStats().BlobSlicesDuplicate == duplicates + 1 );

    // A packet with a message past the size cap is rejected whole: the messages before it too.
    BitWriter liar;
    header( liar );
    liar.WriteBool( true );
    liar.WriteBool( false );
    liar.WriteVarUInt( 4 );
    const uint8_t fine[4] = { 1, 2, 3, 4 };
    liar.WriteBytes( fine, sizeof( fine ) );
    liar.WriteBool( true );
    liar.WriteBool( false );
    liar.WriteVarUInt( NetConnection::kMaxMessageBytes + 1 );
    const std::vector<uint8_t> big( NetConnection::kMaxMessageBytes + 1, 7 );
    liar.WriteBytes( big.data(), big.size() );
    liar.WriteBool( false );
    CHECK_FALSE( receive( connection, liar.GetBytes() ) );
    CHECK( connection.GetStats().PacketsRejected == 2 );
    std::vector<uint8_t> message;
    CHECK_FALSE( connection.ReceiveUnreliable( message ) );
}
