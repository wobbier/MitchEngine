#include "PCH.h"
#include <doctest/doctest.h>
#include "Net/BitStream.h"
#include "Net/NetConnection.h"
#include "Net/NetQuantize.h"
#include "Net/NetTransport.h"
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
                Now = start + kTick * i / kSubSteps;
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
