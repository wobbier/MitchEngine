#include "PCH.h"
#include <doctest/doctest.h>
#include "Components/Networking/NetHitbox.h"
#include "Components/Physics/CharacterController.h"
#include "JSON.h"
#include "Jobs/BackgroundLane.h"
#include "Net/NetReplay.h"
#include "Net/BitStream.h"
#include "Net/NetReplayDump.h"
#include "Net/NetVersion.h"
#include "NetHarness.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <filesystem>
#include <future>
#include <fstream>
#include <map>
#include <random>
#include <sstream>
#include <string>
#include <vector>

// Match replays (ME-316): the .mreplay format round-trips exactly, seeks from any keyframe and
// survives a server that never closed its file.
namespace NetReplayTest
{
    NetReplayHeader MakeHeader()
    {
        NetReplayHeader header;
        header.Product = "ReplayTest 1.0";
        header.EngineCommit = "0123456789abcdef0123456789abcdef01234567";
        header.GameCommit = "fedcba9876543210fedcba9876543210fedcba98";
        header.Map = "Assets/Scenes/Arena.lvl";
        header.CommandLine = "Player --server 0 --record-replay";
        header.RecordedAt = "2026-11-06T12:00:00Z";
        header.TickRate = 60;
        header.NetProtocol = 7;
        header.BuildHash = 0x1111222233334444ull;
        header.ScriptSchemaHash = 0x5555666677778888ull;
        header.SceneHash = 0x99990000AAAABBBBull;
        header.NetSchemaHash = 0xCCCCDDDDEEEEFFFFull;
        return header;
    }

    // A match-like tick: movers that move, a pickup that comes and goes, two clients (one holds
    // its input, one changes it; one misses some commands), shots now and then, events.
    NetReplayTick MakeTick( uint32_t InTick, std::mt19937& InRandom )
    {
        NetReplayTick tick;
        tick.Tick = InTick;
        tick.TicksOwed = InTick % 37 == 0 ? 3 : 0;
        tick.TicksDropped = InTick % 101 == 0 ? 2 : 0;
        tick.State.Tick = InTick;
        for( uint32_t id = 1; id <= 6; ++id )
        {
            NetEntityState& state = tick.State.Entities[id];
            const float t = static_cast<float>( InTick ) / 60.f;
            // Two of them sit still: deltas skip them.
            const float speed = id <= 2 ? 0.f : static_cast<float>( id );
            state.SetPose( Vector3( std::sin( t * speed ) * 5.f, 1.f, std::cos( t ) * speed ),
                Quaternion::FromEulerDegrees( Vector3( 0.f, std::fmod( t * 90.f * speed, 360.f ), 0.f ) ) );
            state.Fields = { static_cast<uint8_t>( id ), static_cast<uint8_t>( InTick / 30 ), static_cast<uint8_t>( id == 4 ? InTick & 0xFF : 7 ) };
        }
        // A dynamic one that's there for 40 ticks in every 100.
        if( InTick % 100 < 40 )
        {
            NetEntityState& pickup = tick.State.Entities[0x10000 + InTick / 100];
            pickup.SetPose( Vector3( 3.f, 0.5f, -2.f ), Quaternion() );
        }
        if( InTick % 100 == 0 )
        {
            tick.Entities.push_back( { 0x10000 + InTick / 100, "Pickup", "Assets/Prefabs/Pickup.prefab", 0 } );
        }

        for( uint8_t id : { uint8_t( 1 ), uint8_t( 3 ) } )
        {
            NetReplayClient& client = tick.Clients.emplace_back();
            client.Id = id;
            InputCommand& command = client.Command;
            command.Tick = InTick + ( id == 3 && InTick % 50 == 7 ? 1 : 0 );
            if( id == 1 )
            {
                // Held for 20 ticks at a time.
                command.Buttons = ( InTick / 20 ) % 2 ? 0x5u : 0x1u;
                command.Axes = { 1.f, 0.f, 0.f, 0.f };
                command.HasAim = true;
                command.Yaw = static_cast<float>( ( InTick / 20 ) * 15 % 360 );
                command.Pitch = -10.f;
                command.ViewTick = static_cast<double>( InTick ) - 6.25;
            }
            else
            {
                command.Buttons = static_cast<uint32_t>( InRandom() & 0xFF );
                command.Axes = { NetCommands::QuantizeAxis( std::sin( InTick * 0.1f ) ), NetCommands::QuantizeAxis( 0.5f ), -1.f, 0.f };
                command.HasAim = InTick % 3 != 0;
                command.Yaw = NetCommands::QuantizeYaw( static_cast<float>( InTick % 360 ) + 0.3f );
                command.Pitch = NetCommands::QuantizePitch( 12.5f );
                // No view yet, an exact 1/64, and one that isn't.
                command.ViewTick = InTick % 5 == 0 ? -1.0 : InTick % 5 == 1 ? static_cast<double>( InTick ) - 0.123456789 : static_cast<double>( InTick ) - 3.0 - 5.0 / 64.0;
            }
            client.Missed = id == 3 && InTick % 13 == 0;
            client.RoundTripMs = 20.f + id + static_cast<float>( InTick / 7 % 7 ) * 1.37f;
            client.PacketLoss = id == 3 ? 0.02f : 0.f;
            client.Slack = 1.5f + 0.01f * static_cast<float>( InTick % 11 );
            client.Misses = InTick / 13;
            client.LateCommands = InTick / 29;
            client.Resyncs = InTick / 400;
        }
        // Only client 1 before tick 30 (joins mid-chunk).
        if( InTick < 30 )
        {
            tick.Clients.pop_back();
        }

        if( InTick % 9 == 0 )
        {
            NetReplayShot& shot = tick.Shots.emplace_back();
            shot.Client = 1;
            shot.Shooter = 3;
            shot.Origin = Vector3( 0.f, 1.6f, 0.f );
            shot.Direction = Vector3( 0.f, 0.f, 1.f );
            shot.Range = 100.f;
            shot.ViewTick = static_cast<double>( InTick ) - 6.25;
            shot.RewoundTicks = 6.25;
            shot.Hit = InTick % 18 == 0;
            shot.Occluded = InTick % 27 == 0;
            shot.Clamped = InTick % 45 == 0;
            shot.Target = shot.Hit ? 4 : 0;
            shot.TargetPart = shot.Hit ? "Head" : "";
            shot.Point = Vector3( 0.f, 1.6f, 9.6f );
            shot.Distance = 9.6f;
            shot.Tested = 2;
            for( uint32_t id : { 4u, 5u } )
            {
                NetReplayHitbox& hitbox = shot.Hitboxes.emplace_back();
                hitbox.NetId = id;
                hitbox.Name = id == 4 ? "Head" : "Body";
                hitbox.Shape = id == 4 ? 0 : 1;
                hitbox.Position = Vector3( 0.f, 1.6f, 10.f );
                hitbox.Rotation = Quaternion::FromEulerDegrees( Vector3( 0.f, 30.f, 0.f ) );
                hitbox.Radius = 0.25f;
                hitbox.HalfHeight = 0.1f;
                hitbox.HalfExtents = Vector3( 0.3f, 0.5f, 0.2f );
                hitbox.Occluder = id == 5;
            }
        }
        // Format 3: movers (two always, one from tick 50 on), one airborne now and then, their
        // [Predicted] bytes changing every 30 ticks.
        for( uint32_t id : { 3u, 4u, 5u } )
        {
            if( id == 5 && InTick < 50 )
            {
                continue;
            }
            NetReplayMover& mover = tick.Movers.emplace_back();
            mover.NetId = id;
            const float t = static_cast<float>( InTick ) / 60.f;
            mover.State.Position = Vector3( std::sin( t * id ) * 5.0001f, 1.f + 0.1234567f * ( InTick % 7 ), std::cos( t ) * id );
            mover.State.Velocity = id == 4 ? Vector3( 0.f, 0.f, 0.f ) : Vector3( std::cos( t * id ) * 5.f, InTick % 40 < 10 ? 3.3333333f : 0.f, -std::sin( t ) );
            mover.State.MoveInput = Vector3( id == 4 ? 0.f : 1.f, 0.f, 0.f );
            mover.State.PendingMove = Vector3( 0.f, 0.f, 0.001f * id );
            mover.State.WantsJump = id == 3 && InTick % 40 == 0;
            mover.State.Grounded = !( id == 3 && InTick % 40 < 10 );
            mover.Predicted = { static_cast<uint8_t>( id ), static_cast<uint8_t>( InTick / 30 ) };
        }
        if( InTick % 9 == 0 )
        {
            // An arrow in flight (Kestrel's): judged against the same hitboxes, fired by #3.
            NetReplayShot sweep = tick.Shots.front();
            sweep.Shooter = 0x10000 + InTick / 100;
            sweep.Instigator = 3;
            sweep.ViewTick = -1.0;
            sweep.RewoundTicks = 0.0;
            sweep.Hit = false;
            sweep.Target = 0;
            sweep.TargetPart.clear();
            tick.Shots.push_back( sweep );
        }
        if( InTick % 61 == 0 )
        {
            tick.Events.push_back( { "kill", "{\"killer\":\"Alpha\",\"victim\":\"Bravo\",\"hero\":\"Striker\"}", 3 } );
        }
        if( InTick == 30 )
        {
            tick.Events.push_back( { "net.joined", "client 3", 0 } );
        }
        return tick;
    }

    std::vector<NetReplayTick> MakeMatch( uint32_t InFirst, uint32_t InCount )
    {
        std::mt19937 random( 316 );
        std::vector<NetReplayTick> ticks;
        for( uint32_t i = 0; i < InCount; ++i )
        {
            ticks.push_back( MakeTick( InFirst + i, random ) );
        }
        return ticks;
    }

    bool SameVector( const Vector3& InA, const Vector3& InB )
    {
        return InA.x == InB.x && InA.y == InB.y && InA.z == InB.z;
    }

    // Exactly the same, except Entities (the recorder rewrites a chunk's first tick's).
    void CheckSame( const NetReplayTick& InA, const NetReplayTick& InB, bool InCompareEntities = true )
    {
        INFO( "tick " << InA.Tick );
        CHECK( InA.Tick == InB.Tick );
        CHECK( InA.TicksOwed == InB.TicksOwed );
        CHECK( InA.TicksDropped == InB.TicksDropped );
        CHECK( InB.State.Tick == InA.Tick );
        CHECK( InA.State.Entities == InB.State.Entities );
        REQUIRE( InA.Clients.size() == InB.Clients.size() );
        for( size_t i = 0; i < InA.Clients.size(); ++i )
        {
            const NetReplayClient& a = InA.Clients[i];
            const NetReplayClient& b = InB.Clients[i];
            CHECK( a.Id == b.Id );
            CHECK( a.Command == b.Command );
            CHECK( a.Command.Yaw == b.Command.Yaw );
            CHECK( a.Command.Pitch == b.Command.Pitch );
            CHECK( a.Missed == b.Missed );
            // The link comes back at its resolution.
            CHECK( std::fabs( a.RoundTripMs - b.RoundTripMs ) <= 0.05f + 1e-4f );
            CHECK( std::fabs( a.PacketLoss - b.PacketLoss ) <= 0.00005f + 1e-7f );
            CHECK( std::fabs( a.Slack - b.Slack ) <= 0.005f + 1e-5f );
            CHECK( a.Misses == b.Misses );
            CHECK( a.LateCommands == b.LateCommands );
            CHECK( a.Resyncs == b.Resyncs );
        }
        REQUIRE( InA.Shots.size() == InB.Shots.size() );
        for( size_t i = 0; i < InA.Shots.size(); ++i )
        {
            const NetReplayShot& a = InA.Shots[i];
            const NetReplayShot& b = InB.Shots[i];
            CHECK( a.Client == b.Client );
            CHECK( a.Shooter == b.Shooter );
            CHECK( a.Instigator == b.Instigator );
            CHECK( SameVector( a.Origin, b.Origin ) );
            CHECK( SameVector( a.Direction, b.Direction ) );
            CHECK( a.Range == b.Range );
            CHECK( a.ViewTick == b.ViewTick );
            CHECK( a.RewoundTicks == b.RewoundTicks );
            CHECK( a.Hit == b.Hit );
            CHECK( a.Occluded == b.Occluded );
            CHECK( a.Clamped == b.Clamped );
            CHECK( a.Rejected == b.Rejected );
            CHECK( a.Target == b.Target );
            CHECK( a.TargetPart == b.TargetPart );
            CHECK( SameVector( a.Point, b.Point ) );
            CHECK( a.Distance == b.Distance );
            CHECK( a.Tested == b.Tested );
            REQUIRE( a.Hitboxes.size() == b.Hitboxes.size() );
            for( size_t h = 0; h < a.Hitboxes.size(); ++h )
            {
                CHECK( a.Hitboxes[h].NetId == b.Hitboxes[h].NetId );
                CHECK( a.Hitboxes[h].Name == b.Hitboxes[h].Name );
                CHECK( a.Hitboxes[h].Shape == b.Hitboxes[h].Shape );
                CHECK( SameVector( a.Hitboxes[h].Position, b.Hitboxes[h].Position ) );
                CHECK( a.Hitboxes[h].Rotation.x == b.Hitboxes[h].Rotation.x );
                CHECK( a.Hitboxes[h].Rotation.w == b.Hitboxes[h].Rotation.w );
                CHECK( a.Hitboxes[h].Radius == b.Hitboxes[h].Radius );
                CHECK( a.Hitboxes[h].HalfHeight == b.Hitboxes[h].HalfHeight );
                CHECK( SameVector( a.Hitboxes[h].HalfExtents, b.Hitboxes[h].HalfExtents ) );
                CHECK( a.Hitboxes[h].Occluder == b.Hitboxes[h].Occluder );
            }
        }
        REQUIRE( InA.Movers.size() == InB.Movers.size() );
        for( size_t i = 0; i < InA.Movers.size(); ++i )
        {
            CHECK( InA.Movers[i].NetId == InB.Movers[i].NetId );
            CHECK( InA.Movers[i].State == InB.Movers[i].State );   // bit for bit
            CHECK( InA.Movers[i].Predicted == InB.Movers[i].Predicted );
        }
        REQUIRE( InA.Events.size() == InB.Events.size() );
        for( size_t i = 0; i < InA.Events.size(); ++i )
        {
            CHECK( InA.Events[i].Name == InB.Events[i].Name );
            CHECK( InA.Events[i].Payload == InB.Events[i].Payload );
            CHECK( InA.Events[i].Entity == InB.Events[i].Entity );
        }
        if( InCompareEntities )
        {
            REQUIRE( InA.Entities.size() == InB.Entities.size() );
            for( size_t i = 0; i < InA.Entities.size(); ++i )
            {
                CHECK( InA.Entities[i].NetId == InB.Entities[i].NetId );
                CHECK( InA.Entities[i].Name == InB.Entities[i].Name );
                CHECK( InA.Entities[i].Prefab == InB.Entities[i].Prefab );
                CHECK( InA.Entities[i].Owner == InB.Entities[i].Owner );
            }
        }
    }

    std::filesystem::path TempFile( const char* InName )
    {
        const std::filesystem::path folder = std::filesystem::temp_directory_path() / "MitchReplayTests";
        std::filesystem::create_directories( folder );
        return folder / InName;
    }

    std::vector<uint8_t> ReadAll( const std::filesystem::path& InPath )
    {
        std::ifstream file( InPath, std::ios::binary );
        return std::vector<uint8_t>( std::istreambuf_iterator<char>( file ), std::istreambuf_iterator<char>() );
    }

    void WriteAll( const std::filesystem::path& InPath, const uint8_t* InData, size_t InSize )
    {
        std::ofstream file( InPath, std::ios::binary | std::ios::trunc );
        file.write( reinterpret_cast<const char*>( InData ), static_cast<std::streamsize>( InSize ) );
    }
}

using namespace NetReplayTest;

TEST_CASE( "Replay: a chunk round-trips states, commands, net stats, shots and events exactly, raw and deflated (ME-316)" )
{
    const std::vector<NetReplayTick> ticks = MakeMatch( 1000, 240 );
    size_t raw = 0;
    const std::vector<uint8_t> plain = NetReplayChunk::Encode( ticks, NetReplayCodec::Raw, &raw );
    const std::vector<uint8_t> deflated = NetReplayChunk::Encode( ticks, NetReplayCodec::Deflate );
    CHECK( plain.size() == raw + NetReplayFormat::kChunkHeaderBytes );
    // Even with one client mashing random buttons, deflate takes a good share off.
    CHECK( deflated.size() < plain.size() * 3 / 4 );
    MESSAGE( "240 ticks: " << raw << " bytes raw, " << deflated.size() << " deflated" );

    for( const std::vector<uint8_t>* bytes : { &plain, &deflated } )
    {
        NetReplayChunk::Info info;
        REQUIRE( NetReplayChunk::ReadInfo( bytes->data(), bytes->size(), info ) );
        CHECK( info.FirstTick == 1000 );
        CHECK( info.LastTick == 1239 );
        CHECK( info.TickCount == 240 );
        std::vector<NetReplayTick> decoded;
        std::string error;
        REQUIRE_MESSAGE( NetReplayChunk::Decode( bytes->data(), bytes->size(), decoded, &error ), error );
        REQUIRE( decoded.size() == ticks.size() );
        for( size_t i = 0; i < ticks.size(); ++i )
        {
            CheckSame( ticks[i], decoded[i] );
        }
    }

    // A shot whose hitboxes match an earlier one of its tick (the sweep here) points at it rather
    // than repeating them: hitboxes that differ cost their bytes again.
    std::vector<NetReplayTick> distinct = ticks;
    size_t sweeps = 0;
    for( NetReplayTick& tick : distinct )
    {
        for( NetReplayShot& shot : tick.Shots )
        {
            if( shot.Instigator != 0 )
            {
                ++sweeps;
                for( NetReplayHitbox& hitbox : shot.Hitboxes )
                {
                    hitbox.Radius += 0.01f;
                }
            }
        }
    }
    size_t distinctRaw = 0;
    const std::vector<uint8_t> distinctBytes = NetReplayChunk::Encode( distinct, NetReplayCodec::Raw, &distinctRaw );
    MESSAGE( sweeps << " sweeps: " << raw << " bytes sharing their hitboxes, " << distinctRaw << " with their own" );
    CHECK( sweeps >= 26 );
    CHECK( distinctRaw > raw + sweeps * 80 );
    {
        std::vector<NetReplayTick> decoded;
        REQUIRE( NetReplayChunk::Decode( distinctBytes.data(), distinctBytes.size(), decoded ) );
        REQUIRE( decoded.size() == distinct.size() );
        for( size_t i = 0; i < distinct.size(); ++i )
        {
            CheckSame( distinct[i], decoded[i] );
        }
    }

    // A flipped byte or a short read is refused, not misread.
    std::vector<uint8_t> torn = deflated;
    torn[NetReplayFormat::kChunkHeaderBytes + torn.size() / 3] ^= 0x40;
    std::vector<NetReplayTick> decoded;
    std::string error;
    CHECK_FALSE( NetReplayChunk::Decode( torn.data(), torn.size(), decoded, &error ) );
    CHECK( error.find( "checksum" ) != std::string::npos );
    CHECK_FALSE( NetReplayChunk::Decode( deflated.data(), deflated.size() - 1, decoded, &error ) );

    // The header too.
    const NetReplayHeader header = MakeHeader();
    const std::vector<uint8_t> headerBytes = NetReplayFile::EncodeHeader( header );
    NetReplayHeader read;
    size_t size = 0;
    REQUIRE( NetReplayFile::DecodeHeader( headerBytes.data(), headerBytes.size(), read, size ) );
    CHECK( size == headerBytes.size() );
    CHECK( read.Product == header.Product );
    CHECK( read.EngineCommit == header.EngineCommit );
    CHECK( read.GameCommit == header.GameCommit );
    CHECK( read.Map == header.Map );
    CHECK( read.CommandLine == header.CommandLine );
    CHECK( read.RecordedAt == header.RecordedAt );
    CHECK( read.TickRate == 60 );
    CHECK( read.NetProtocol == 7 );
    CHECK( read.BuildHash == header.BuildHash );
    CHECK( read.ScriptSchemaHash == header.ScriptSchemaHash );
    CHECK( read.SceneHash == header.SceneHash );
    CHECK( read.NetSchemaHash == header.NetSchemaHash );
    CHECK_FALSE( NetReplayFile::DecodeHeader( headerBytes.data(), 12, read, size ) );
}

namespace NetReplayTest
{
    // Format 2's chunk, as 29665987 wrote it (no movers, no instigators, every shot's hitboxes in
    // full; clients left out here): what replays kept from before format 3 hold.
    void PutU32( std::vector<uint8_t>& InOut, uint32_t InValue )
    {
        for( int i = 0; i < 4; ++i )
        {
            InOut.push_back( static_cast<uint8_t>( InValue >> ( 8 * i ) ) );
        }
    }

    void WriteVector2( BitWriter& InWriter, const Vector3& InValue )
    {
        InWriter.WriteFloat( InValue.x );
        InWriter.WriteFloat( InValue.y );
        InWriter.WriteFloat( InValue.z );
    }

    void WriteDouble2( BitWriter& InWriter, double InValue )
    {
        uint64_t bits = 0;
        std::memcpy( &bits, &InValue, sizeof( bits ) );
        InWriter.WriteBits( static_cast<uint32_t>( bits ), 32 );
        InWriter.WriteBits( static_cast<uint32_t>( bits >> 32 ), 32 );
    }

    std::vector<uint8_t> EncodeFormat2Chunk( const std::vector<NetReplayTick>& InTicks )
    {
        BitWriter payload;
        payload.WriteVarUInt( InTicks.size() );
        const NetReplayTick* previous = nullptr;
        for( const NetReplayTick& tick : InTicks )
        {
            if( previous )
            {
                payload.WriteVarUInt( tick.Tick - previous->Tick );
            }
            else
            {
                payload.WriteBits( tick.Tick, 32 );
            }
            payload.WriteVarUInt( tick.TicksOwed );
            payload.WriteVarUInt( tick.TicksDropped );
            NetSnapshot sent;
            NetSnapshotCodec::Write( payload, tick.State, previous ? &previous->State : nullptr, std::numeric_limits<size_t>::max() / 2, sent );
            payload.WriteVarUInt( tick.Entities.size() );
            for( const NetReplayEntity& entity : tick.Entities )
            {
                payload.WriteVarUInt( entity.NetId );
                payload.WriteString( entity.Name );
                payload.WriteString( entity.Prefab );
                payload.WriteBits( entity.Owner, 8 );
            }
            payload.WriteVarUInt( 0 );   // clients
            payload.WriteVarUInt( tick.Shots.size() );
            for( const NetReplayShot& shot : tick.Shots )
            {
                payload.WriteBits( shot.Client, 8 );
                payload.WriteVarUInt( shot.Shooter );
                WriteVector2( payload, shot.Origin );
                WriteVector2( payload, shot.Direction );
                payload.WriteFloat( shot.Range );
                WriteDouble2( payload, shot.ViewTick );
                WriteDouble2( payload, shot.RewoundTicks );
                payload.WriteBool( shot.Hit );
                payload.WriteBool( shot.Occluded );
                payload.WriteBool( shot.Clamped );
                payload.WriteBool( shot.Rejected );
                payload.WriteVarUInt( shot.Target );
                payload.WriteString( shot.TargetPart );
                WriteVector2( payload, shot.Point );
                payload.WriteFloat( shot.Distance );
                payload.WriteVarUInt( shot.Tested );
                payload.WriteVarUInt( shot.Hitboxes.size() );
                for( const NetReplayHitbox& hitbox : shot.Hitboxes )
                {
                    payload.WriteVarUInt( hitbox.NetId );
                    payload.WriteString( hitbox.Name );
                    payload.WriteBits( hitbox.Shape, 8 );
                    WriteVector2( payload, hitbox.Position );
                    payload.WriteFloat( hitbox.Rotation.x );
                    payload.WriteFloat( hitbox.Rotation.y );
                    payload.WriteFloat( hitbox.Rotation.z );
                    payload.WriteFloat( hitbox.Rotation.w );
                    payload.WriteFloat( hitbox.Radius );
                    payload.WriteFloat( hitbox.HalfHeight );
                    WriteVector2( payload, hitbox.HalfExtents );
                    payload.WriteBool( hitbox.Occluder );
                }
            }
            payload.WriteVarUInt( tick.Events.size() );
            for( const NetReplayEvent& event : tick.Events )
            {
                payload.WriteString( event.Name );
                payload.WriteString( event.Payload );
                payload.WriteVarUInt( event.Entity );
            }
            previous = &tick;
        }
        const std::vector<uint8_t>& raw = payload.GetBytes();
        std::vector<uint8_t> out;
        PutU32( out, NetReplayFormat::kChunkMagic );
        PutU32( out, InTicks.front().Tick );
        PutU32( out, InTicks.back().Tick );
        PutU32( out, static_cast<uint32_t>( InTicks.size() ) );
        out.insert( out.end(), { static_cast<uint8_t>( NetReplayCodec::Raw ), 0, 0, 0 } );
        PutU32( out, static_cast<uint32_t>( raw.size() ) );
        PutU32( out, static_cast<uint32_t>( raw.size() ) );
        const uint64_t checksum = NetVersion::Hash( std::string_view( reinterpret_cast<const char*>( raw.data() ), raw.size() ) );
        PutU32( out, static_cast<uint32_t>( checksum ) );
        PutU32( out, static_cast<uint32_t>( checksum >> 32 ) );
        out.insert( out.end(), raw.begin(), raw.end() );
        return out;
    }
}

TEST_CASE( "Replay: format 3 still reads format 2 replays (kept ones open in a newer ReplayDump) and refuses formats it doesn't know (ME-316)" )
{
    // What format 2 could hold: no clients here (their encoding didn't change), no movers, no instigators.
    std::vector<NetReplayTick> ticks = MakeMatch( 0, 120 );
    for( NetReplayTick& tick : ticks )
    {
        tick.Clients.clear();
        tick.Movers.clear();
        for( NetReplayShot& shot : tick.Shots )
        {
            shot.Instigator = 0;
        }
    }
    const std::vector<uint8_t> chunk = EncodeFormat2Chunk( ticks );
    std::vector<NetReplayTick> decoded;
    std::string error;
    REQUIRE_MESSAGE( NetReplayChunk::Decode( chunk.data(), chunk.size(), decoded, &error, 2 ), error );
    REQUIRE( decoded.size() == ticks.size() );
    for( size_t i = 0; i < ticks.size(); ++i )
    {
        CheckSame( ticks[i], decoded[i] );
    }
    CHECK_FALSE( NetReplayChunk::Decode( chunk.data(), chunk.size(), decoded, &error, 1 ) );
    CHECK( error.find( "replay format 1" ) != std::string::npos );
    CHECK_FALSE( NetReplayChunk::Decode( chunk.data(), chunk.size(), decoded, &error, NetReplayFormat::kVersion + 1 ) );

    // A whole format 2 file (a dead server's: no index), through the file source and the reader.
    std::vector<uint8_t> file = NetReplayFile::EncodeHeader( MakeHeader() );
    auto withVersion = [&file]( uint32_t InVersion ) {
        std::vector<uint8_t> bytes = file;
        for( int i = 0; i < 4; ++i )
        {
            bytes[8 + i] = static_cast<uint8_t>( InVersion >> ( 8 * i ) );
        }
        return bytes;
    };
    NetReplayHeader header;
    size_t headerBytes = 0;
    for( uint32_t refused : { 1u, NetReplayFormat::kVersion + 1 } )
    {
        const std::vector<uint8_t> bytes = withVersion( refused );
        CHECK_FALSE( NetReplayFile::DecodeHeader( bytes.data(), bytes.size(), header, headerBytes, &error ) );
    }
    file = withVersion( 2 );
    file.insert( file.end(), chunk.begin(), chunk.end() );
    const std::filesystem::path path = TempFile( "Format2.mreplay" );
    WriteAll( path, file.data(), file.size() );
    NetReplayFileSource source;
    REQUIRE_MESSAGE( source.Open( path.string() ), source.GetError() );
    CHECK( source.GetHeader().FormatVersion == 2 );
    CHECK( source.GetHeader().Map == "Assets/Scenes/Arena.lvl" );
    NetReplayReader reader( source );
    NetReplayTick tick;
    REQUIRE_MESSAGE( reader.ReadTick( 99, tick ), reader.GetError() );
    CheckSame( ticks[99], tick );
    std::filesystem::remove( path );
}

TEST_CASE( "Replay: the recorder writes chunks on the background lane, and every tick reads back from its keyframe in any order (ME-316)" )
{
    const std::filesystem::path path = TempFile( "Recorded.mreplay" );
    std::filesystem::remove( path );
    const std::vector<NetReplayTick> ticks = MakeMatch( 500, 637 );
    NetReplaySettings settings;
    settings.KeyframeInterval = 60;
    settings.RingSeconds = 2.0;
    {
        NetReplayRecorder recorder( MakeHeader(), settings );
        REQUIRE( recorder.OpenFile( path.string() ) );
        for( const NetReplayTick& tick : ticks )
        {
            recorder.Record( NetReplayTick( tick ) );
        }
        // The ring keeps the newest two seconds.
        REQUIRE( recorder.GetRing().size() == 120 );
        CHECK( recorder.GetRing().front()->Tick == ticks[ticks.size() - 120].Tick );
        CHECK( recorder.GetRing().back()->Tick == ticks.back().Tick );
        recorder.Close();
        CHECK_FALSE( recorder.HasFailed() );
        CHECK( recorder.GetTicksRecorded() == ticks.size() );
        CHECK( recorder.GetChunksWritten() == 11 );   // 10 x 60 + 37
        CHECK( recorder.GetBytesWritten() == std::filesystem::file_size( path ) );
        CHECK( recorder.GetRawBytes() > recorder.GetBytesWritten() );
    }

    NetReplayFileSource source;
    REQUIRE_MESSAGE( source.Open( path.string() ), source.GetError() );
    CHECK( source.HasIndex() );
    CHECK( source.GetHeader().KeyframeInterval == 60 );
    CHECK( source.GetHeader().Map == "Assets/Scenes/Arena.lvl" );
    REQUIRE( source.GetChunkCount() == 11 );
    NetReplayReader reader( source );
    CHECK( reader.GetFirstTick() == 500 );
    CHECK( reader.GetLastTick() == 1136 );
    CHECK( reader.GetTickCount() == ticks.size() );

    // Every tick, in a shuffled order: each seek decodes its chunk from that chunk's keyframe.
    std::vector<size_t> order( ticks.size() );
    for( size_t i = 0; i < order.size(); ++i )
    {
        order[i] = i;
    }
    std::shuffle( order.begin(), order.end(), std::mt19937( 202 ) );
    for( const size_t i : order )
    {
        NetReplayTick tick;
        REQUIRE( reader.ReadTick( ticks[i].Tick, tick ) );
        // A chunk's first tick names every live entity; the others carry what changed.
        const bool chunkStart = ( i % 60 ) == 0;
        CheckSame( ticks[i], tick, !chunkStart );
        if( chunkStart )
        {
            for( const auto& [id, state] : ticks[i].State.Entities )
            {
                if( id >= 0x10000 )
                {
                    const bool named = std::any_of( tick.Entities.begin(), tick.Entities.end(), [id = id]( const NetReplayEntity& InEntity ) { return InEntity.NetId == id; } );
                    CHECK_MESSAGE( named, "chunk at tick " << tick.Tick << " doesn't name " << id );
                }
            }
        }
    }
    NetReplayTick missing;
    CHECK_FALSE( reader.ReadTick( 499, missing ) );
    CHECK_FALSE( reader.ReadTick( 1137, missing ) );

    // A range, in order.
    std::vector<uint32_t> seen;
    REQUIRE( reader.ForEach( 590, 700, [&seen]( const NetReplayTick& InTick ) {
        seen.push_back( InTick.Tick );
        return true;
    } ) );
    REQUIRE( seen.size() == 111 );
    CHECK( seen.front() == 590 );
    CHECK( seen.back() == 700 );

    // The same chunks in memory (a stream): the reader doesn't care where they came from.
    NetReplayMemorySource memory( source.GetHeader() );
    for( size_t i = 0; i < source.GetChunkCount(); ++i )
    {
        std::vector<uint8_t> bytes;
        REQUIRE( source.ReadChunk( i, bytes ) );
        REQUIRE( memory.AddChunk( std::move( bytes ) ) );
    }
    NetReplayReader fromMemory( memory );
    NetReplayTick tick;
    REQUIRE( fromMemory.ReadTick( 1000, tick ) );
    CheckSame( ticks[500], tick );
    std::filesystem::remove( path );
}

TEST_CASE( "Replay: a file whose server died (no index, a torn last chunk) reads up to its last whole chunk (ME-316)" )
{
    const std::filesystem::path path = TempFile( "Crashed.mreplay" );
    std::filesystem::remove( path );
    const std::vector<NetReplayTick> ticks = MakeMatch( 0, 200 );
    NetReplaySettings settings;
    settings.KeyframeInterval = 50;
    {
        NetReplayRecorder recorder( MakeHeader(), settings );
        REQUIRE( recorder.OpenFile( path.string() ) );
        for( const NetReplayTick& tick : ticks )
        {
            recorder.Record( NetReplayTick( tick ) );
        }
        recorder.Close();
    }
    // Cut the index off and half the last chunk.
    NetReplayFileSource whole;
    REQUIRE( whole.Open( path.string() ) );
    REQUIRE( whole.GetChunkCount() == 4 );
    std::vector<uint8_t> last;
    REQUIRE( whole.ReadChunk( 3, last ) );
    const std::vector<uint8_t> bytes = ReadAll( path );
    size_t lastAt = 0;
    for( size_t at = 0; at + last.size() <= bytes.size(); ++at )
    {
        if( std::equal( last.begin(), last.end(), bytes.begin() + static_cast<std::ptrdiff_t>( at ) ) )
        {
            lastAt = at;
            break;
        }
    }
    REQUIRE( lastAt > 0 );
    const std::filesystem::path torn = TempFile( "Torn.mreplay" );
    WriteAll( torn, bytes.data(), lastAt + last.size() / 2 );

    NetReplayFileSource source;
    REQUIRE_MESSAGE( source.Open( torn.string() ), source.GetError() );
    CHECK_FALSE( source.HasIndex() );
    CHECK( source.GetChunkCount() == 3 );
    NetReplayReader reader( source );
    CHECK( reader.GetLastTick() == 149 );
    NetReplayTick tick;
    REQUIRE( reader.ReadTick( 149, tick ) );
    CheckSame( ticks[149], tick );
    CHECK_FALSE( reader.ReadTick( 150, tick ) );
    std::filesystem::remove( path );
    std::filesystem::remove( torn );
}

TEST_CASE( "Replay: a recording server that dies loses about a second, not a chunk: the open chunk is on disk as a provisional tail (ME-316)" )
{
    const std::filesystem::path path = TempFile( "Tail.mreplay" );
    std::filesystem::remove( path );
    const std::vector<NetReplayTick> ticks = MakeMatch( 0, 310 );
    NetReplaySettings settings;
    settings.KeyframeInterval = 100;
    settings.FlushSeconds = 0.5;    // every 30 ticks at 60 Hz
    NetReplayRecorder recorder( MakeHeader(), settings );
    REQUIRE( recorder.OpenFile( path.string() ) );
    auto record = [&]( size_t InFrom, size_t InTo ) {
        for( size_t i = InFrom; i < InTo; ++i )
        {
            recorder.Record( NetReplayTick( ticks[i] ) );
        }
        recorder.WaitForWrites();
    };
    // What a reader finds if the server died now (no index: the chunks are scanned).
    auto lastOnDisk = [&]( size_t InChunks ) {
        NetReplayFileSource source;
        REQUIRE_MESSAGE( source.Open( path.string() ), source.GetError() );
        CHECK_FALSE( source.HasIndex() );
        CHECK( source.GetChunkCount() == InChunks );
        NetReplayReader reader( source );
        NetReplayTick tick;
        const uint32_t last = reader.GetLastTick();
        REQUIRE( reader.ReadTick( last, tick ) );
        CheckSame( ticks[last], tick );
        return last;
    };

    record( 0, 200 );
    CHECK( lastOnDisk( 2 ) == 199 );
    record( 200, 230 );                 // 30 ticks into the third chunk: a tail
    CHECK( lastOnDisk( 3 ) == 229 );
    record( 230, 265 );                 // the next tail overwrites it
    CHECK( lastOnDisk( 3 ) == 259 );
    record( 265, 300 );                 // the whole chunk overwrites the tail
    CHECK( lastOnDisk( 3 ) == 299 );
    CHECK( recorder.GetChunksWritten() == 3 );
    record( 300, 310 );
    recorder.Close();
    CHECK_FALSE( recorder.HasFailed() );
    // Closed: the index after the whole chunks, nothing of a tail left over.
    CHECK( recorder.GetBytesWritten() == std::filesystem::file_size( path ) );
    NetReplayFileSource source;
    REQUIRE( source.Open( path.string() ) );
    CHECK( source.HasIndex() );
    REQUIRE( source.GetChunkCount() == 4 );
    NetReplayReader reader( source );
    CHECK( reader.GetTickCount() == 310 );
    for( uint32_t t : { 0u, 150u, 229u, 299u, 309u } )
    {
        NetReplayTick tick;
        REQUIRE( reader.ReadTick( t, tick ) );
        CheckSame( ticks[t], tick, t % 100 != 0 );
    }
    std::filesystem::remove( path );
}

TEST_CASE( "Replay: a ring-only recorder names every entity only at keyframes, like the file; its table says what each NetId is (ME-316)" )
{
    NetReplaySettings settings;
    settings.KeyframeInterval = 50;
    settings.RingSeconds = 3.0;
    NetReplayRecorder recorder( MakeHeader(), settings );
    for( NetReplayTick& tick : MakeMatch( 0, 140 ) )
    {
        recorder.Record( std::move( tick ) );
    }
    REQUIRE( recorder.GetRing().size() == 140 );
    for( const auto& tick : recorder.GetRing() )
    {
        INFO( "tick " << tick->Tick );
        if( tick->Tick % 50 == 0 )
        {
            CHECK( tick->Entities.size() == tick->State.Entities.count( 0x10000 + tick->Tick / 100 ) );
        }
        else
        {
            // Only what appeared this tick (MakeTick names its pickup when it spawns).
            CHECK( tick->Entities.size() == ( tick->Tick % 100 == 0 ? 1u : 0u ) );
        }
    }
    // Tick 139's live pickup (0x10001, there since 100) is in the table; 0x10000 went at 40.
    CHECK_FALSE( recorder.GetEntities().count( 0x10000 ) );
    REQUIRE( recorder.GetEntities().count( 0x10001 ) );
    CHECK( recorder.GetEntities().at( 0x10001 ).Name == "Pickup" );
}

TEST_CASE( "Replay: a disk that can't keep up stops the file past MaxBacklogSeconds instead of holding chunks without end (ME-316)" )
{
    const std::filesystem::path path = TempFile( "Backlog.mreplay" );
    std::filesystem::remove( path );
    NetReplaySettings settings;
    settings.KeyframeInterval = 1;
    settings.MaxBacklogSeconds = 2.0 / 60.0;    // two chunks waiting
    settings.FlushSeconds = 0.0;
    NetReplayRecorder recorder( MakeHeader(), settings );
    REQUIRE( recorder.OpenFile( path.string() ) );
    // A stalled disk: every lane thread is held, so nothing the recorder queues gets written.
    std::promise<void> release;
    std::shared_future<void> released = release.get_future().share();
    Jobs::BackgroundLane& lane = Jobs::BackgroundLane::Get();
    std::vector<std::future<void>> holds;
    for( uint32_t i = 0; i < lane.GetThreadCount(); ++i )
    {
        holds.push_back( lane.Submit( "Replay test: stalled disk", [released]() { released.wait(); } ) );
    }
    for( NetReplayTick& tick : MakeMatch( 0, 6 ) )
    {
        recorder.Record( std::move( tick ) );   // a chunk each (interval 1)
    }
    CHECK( recorder.HasFailed() );
    CHECK( recorder.GetError().find( "can't keep up" ) != std::string::npos );
    // Let go of: not held while the disk is stuck.
    CHECK( recorder.GetPendingWrites() <= 2 );
    CHECK( recorder.GetTicksRecorded() == 6 );
    CHECK( recorder.GetRing().size() == 6 );    // the ring goes on
    release.set_value();
    for( std::future<void>& hold : holds )
    {
        hold.wait();
    }
    recorder.Close();
    CHECK( recorder.GetChunksWritten() == 0 );
    std::filesystem::remove( path );
}

TEST_CASE( "Replay: a server records every tick's capture, commands, joins and lag-compensated shots (with the hitboxes where they were rewound to); the file at tick N is the live capture at tick N (ME-316)" )
{
    using namespace NetHarness;
    NetConditions toClient;
    toClient.LatencyMs = 40.f;
    toClient.Seed = 61;
    NetConditions toServer = toClient;
    toServer.Seed = 62;
    Harness net( toClient, toServer );
    net.Place( 0x7A26E7 );     // the target
    net.Place( 0x5400 );       // the shooter, owned by the client
    net.Place( 0xA770 );       // its arrow: unowned, sweeping its flight
    net.Place( 0x2A11 );       // a character (its mover goes in the replay)
    EntityHandle target;
    EntityHandle shooter;
    EntityHandle arrow;
    EntityHandle runner;
    net.ServerWorld->ForEachEntity( [&]( Entity& InEntity ) {
        if( InEntity.GetGUID() == 0x7A26E7 ) target = net.ServerWorld->GetEntity( InEntity.GetId() );
        if( InEntity.GetGUID() == 0x5400 ) shooter = net.ServerWorld->GetEntity( InEntity.GetId() );
        if( InEntity.GetGUID() == 0xA770 ) arrow = net.ServerWorld->GetEntity( InEntity.GetId() );
        if( InEntity.GetGUID() == 0x2A11 ) runner = net.ServerWorld->GetEntity( InEntity.GetId() );
    } );
    arrow->GetComponent<Transform>().SetPosition( Vector3( 0.f, 1.f, -20.f ) );
    CharacterController& runnerMover = runner->AddComponent<CharacterController>();
    runner->GetComponent<Transform>().SetPosition( Vector3( 0.f, 0.f, 30.f ) );
    target->SetName( "Target" );
    shooter->SetName( "Shooter" );
    NetHitbox& hitbox = target->AddComponent<NetHitbox>();
    hitbox.Radius = 0.4f;
    hitbox.Height = 1.8f;
    shooter->GetComponent<Transform>().SetPosition( Vector3( 0.f, 1.f, -10.f ) );
    shooter->GetComponent<NetIdentity>().SetOwner( 1 );
    net.ServerWorld->Simulate();
    net.ServerWorld->Start();

    // Fire every 15 ticks straight down +Z; the target strafes across it.
    net.MakeCommand = []( uint32_t InTick ) {
        InputCommand command;
        command.Buttons = InTick % 15 == 0 ? 2u : 0u;
        command.Axes = { 0.f, 0.f, 1.f, 0.f };
        command.HasAim = true;
        command.Yaw = NetCommands::QuantizeYaw( static_cast<float>( InTick % 360 ) );
        return command;
    };
    std::map<uint32_t, NetSnapshot> live;
    std::map<uint32_t, InputCommand> commands;
    std::map<uint32_t, LagCompensatedHit> shots;
    std::map<uint32_t, uint32_t> newestAtShot;   // the lag-compensation history's newest tick when it fired
    net.ServerNet.SetCommandHandler( [&]( Entity& InShooter, const InputCommand& InCommand ) {
        commands[net.ServerNet.GetServer()->GetTick()] = InCommand;
        if( !( InCommand.Buttons & 2u ) )
        {
            return;
        }
        const EntityHandle self = net.ServerWorld->GetEntity( InShooter.GetId() );
        newestAtShot[net.ServerNet.GetServer()->GetTick()] = net.ServerNet.GetLagCompensation().GetNewestTick();
        shots[net.ServerNet.GetServer()->GetTick()] = net.ServerNet.LagCompensatedRaycast( Vector3( 0.f, 1.f, -10.f ), Vector3( 0.f, 0.f, 1.f ), 100.f, InCommand, self );
        // The arrow it loosed earlier sweeps a step of its flight: judged now, the shot the shooter's.
        InputCommand present;
        present.ViewTick = -1.0;
        net.ServerNet.LagCompensatedRaycast( Vector3( 0.f, 1.f, -20.f ), Vector3( 0.f, 0.f, 1.f ), 5.f, present, arrow, self );
    } );
    // The runner's mover, set every tick off the 1/1024 m grid snapshots quantize to, airborne
    // with a velocity half the time: the replay keeps it exactly.
    std::map<uint32_t, CharacterController::NetState> moverAt;
    auto runnerState = []( uint32_t InTick ) {
        CharacterController::NetState state;
        const float t = static_cast<float>( InTick ) / 60.f;
        state.Position = Vector3( std::sin( t ) * 4.00001f, 0.123456789f * static_cast<float>( InTick % 5 ), 30.f + t * 0.3333f );
        state.Velocity = InTick % 60 < 30 ? Vector3( std::cos( t ) * 4.f, 2.5f - 0.1f * static_cast<float>( InTick % 30 ), 0.3333f ) : Vector3();
        state.MoveInput = Vector3( 1.f, 0.f, 0.f );
        state.PendingMove = Vector3( 0.f, 0.f, 0.0001f * static_cast<float>( InTick % 3 ) );
        state.WantsJump = InTick % 60 == 0;
        state.Grounded = InTick % 60 >= 30;
        return state;
    };
    // The target's x at every tick (the lag-compensation history records it at the tick's end).
    std::map<uint32_t, float> targetX;
    // A networked entity spawned mid-tick, and an event about it in the same tick: CaptureTick gives
    // it its NetId only at the tick's end, and the event still names it.
    EntityHandle spawned;
    uint32_t spawnedAt = 0;
    net.OnServerTick = [&]( NetServer& InServer ) {
        // The capture so far is the tick before's.
        if( InServer.GetTick() > 0 )
        {
            live[net.ServerNet.GetReplicationServer()->GetCurrent().Tick] = net.ServerNet.GetReplicationServer()->GetCurrent();
        }
        const float x = std::sin( static_cast<float>( InServer.GetTick() ) * 0.05f ) * 3.f;
        targetX[InServer.GetTick()] = x;
        target->GetComponent<Transform>().SetPosition( Vector3( x, 1.f, 10.f ) );
        runnerMover.SetNetState( runnerState( InServer.GetTick() ) );
        moverAt[InServer.GetTick()] = runnerMover.GetNetState();
        if( !spawned && net.ServerNet.IsRecording() && InServer.GetTick() >= 200 )
        {
            spawned = net.ServerWorld->CreateEntity();
            spawned->SetName( "Spawned" );
            spawned->AddComponent<Transform>();
            spawned->AddComponent<NetIdentity>();
            spawnedAt = InServer.GetTick();
            CHECK( net.ServerNet.GetNetId( *spawned.Get() ) == 0u );
            net.ServerNet.RecordReplayEvent( "spawn", "{}", spawned );
        }
        net.ServerWorld->Simulate();
    };
    const std::filesystem::path path = TempFile( "Session.mreplay" );
    std::filesystem::remove( path );
    net.Start();
    NetReplayHeader header;
    header.Map = "Harness";
    NetReplaySettings settings;
    settings.KeyframeInterval = 45;
    REQUIRE( net.ServerNet.StartRecording( path.string(), header, settings ) );
    CHECK( net.ServerNet.IsRecording() );
    CHECK_FALSE( net.ClientNet.StartRecording( path.string() + ".client" ) );   // a server's job
    net.Run( 6.0 );
    net.ServerNet.RecordReplayEvent( "kill", "{\"killer\":1}", net.ServerNet.GetNetId( *shooter.Get() ) );
    net.Run( 0.1 );
    net.ServerNet.StopRecording();
    CHECK_FALSE( net.ServerNet.IsRecording() );
    const NetReplayRecorder* recorder = net.ServerNet.GetRecorder();
    REQUIRE( recorder );
    CHECK_FALSE( recorder->HasFailed() );

    NetReplayFileSource source;
    REQUIRE_MESSAGE( source.Open( path.string() ), source.GetError() );
    CHECK( source.GetHeader().TickRate == 60 );
    CHECK( source.GetHeader().Map == "Harness" );
    CHECK( source.GetHeader().SceneHash == net.ServerNet.GetServer()->GetVersion().Scene );
    CHECK( source.GetHeader().NetSchemaHash == NetSchema::Hash() );
    NetReplayReader reader( source );
    CHECK( reader.GetTickCount() == recorder->GetTicksRecorded() );

    size_t compared = 0;
    size_t commandsCompared = 0;
    size_t shotsCompared = 0;
    size_t withRoundTrip = 0;
    size_t rewoundAway = 0;
    size_t moversCompared = 0;
    bool joined = false;
    bool killed = false;
    bool spawnNamed = false;
    std::map<uint32_t, std::string> names;
    REQUIRE( reader.ForEach( 0, ~0u, [&]( const NetReplayTick& InTick ) {
        // The state played back at tick N is the server's live capture at tick N.
        if( auto it = live.find( InTick.Tick ); it != live.end() )
        {
            CHECK( InTick.State.Entities == it->second.Entities );
            ++compared;
        }
        for( const NetReplayEntity& entity : InTick.Entities )
        {
            names[entity.NetId] = entity.Name;
        }
        if( auto it = commands.find( InTick.Tick ); it != commands.end() )
        {
            REQUIRE( InTick.Clients.size() == 1 );
            CHECK( InTick.Clients[0].Id == 1 );
            CHECK( InTick.Clients[0].Command == it->second );
            // The round trip, from the first ack on (80 ms of link plus the tick).
            withRoundTrip += InTick.Clients[0].RoundTripMs > 60.f ? 1 : 0;
            ++commandsCompared;
        }
        // The runner's mover after the tick, at full precision (the only character).
        if( auto it = moverAt.find( InTick.Tick ); it != moverAt.end() )
        {
            REQUIRE( InTick.Movers.size() == 1 );
            CHECK( InTick.Movers[0].NetId == net.ServerNet.GetNetId( *runner.Get() ) );
            CHECK( InTick.Movers[0].State == it->second );
            CHECK( InTick.Movers[0].Predicted.empty() );    // no scripts in the harness
            ++moversCompared;
        }
        if( auto it = shots.find( InTick.Tick ); it != shots.end() )
        {
            REQUIRE( InTick.Shots.size() == 2 );
            // The sweep: the arrow's ray, the shooter's shot (its client), not a bot's.
            const NetReplayShot& sweep = InTick.Shots[1];
            CHECK( sweep.Shooter == net.ServerNet.GetNetId( *arrow.Get() ) );
            CHECK( sweep.Instigator == net.ServerNet.GetNetId( *shooter.Get() ) );
            CHECK( sweep.Client == 1 );
            CHECK( sweep.RewoundTicks == 0.0 );
            const NetReplayShot& shot = InTick.Shots[0];
            CHECK( shot.Instigator == 0u );
            CHECK( shot.Client == 1 );
            CHECK( shot.Shooter == net.ServerNet.GetNetId( *shooter.Get() ) );
            CHECK( shot.Hit == it->second.Hit );
            CHECK( shot.RewoundTicks == it->second.RewoundTicks );
            CHECK( shot.Target == ( it->second.Hit ? net.ServerNet.GetNetId( *target.Get() ) : 0u ) );
            // The target's hitbox, rewound: the shooter has none.
            REQUIRE( shot.Hitboxes.size() == 1 );
            CHECK( shot.Hitboxes[0].NetId == net.ServerNet.GetNetId( *target.Get() ) );
            CHECK( shot.Hitboxes[0].Name == "Target" );
            // ... where the target was at the judged tick (newest - RewoundTicks, between two recorded
            // ticks), not where it is now.
            const double judged = static_cast<double>( newestAtShot[InTick.Tick] ) - shot.RewoundTicks;
            const uint32_t before = static_cast<uint32_t>( std::floor( judged ) );
            const float alpha = static_cast<float>( judged - std::floor( judged ) );
            REQUIRE( targetX.count( before ) );
            REQUIRE( targetX.count( before + 1 ) );
            const float expected = targetX[before] + ( targetX[before + 1] - targetX[before] ) * alpha;
            INFO( "shot at " << InTick.Tick << " judged at " << judged );
            CHECK( std::fabs( shot.Hitboxes[0].Position.x - expected ) < 1e-3f );
            if( std::fabs( targetX[newestAtShot[InTick.Tick]] - expected ) > 0.05f )
            {
                // It moved since: the live pose would have been somewhere else.
                CHECK( std::fabs( shot.Hitboxes[0].Position.x - targetX[newestAtShot[InTick.Tick]] ) > 0.04f );
                ++rewoundAway;
            }
            ++shotsCompared;
        }
        else
        {
            CHECK( InTick.Shots.empty() );
        }
        for( const NetReplayEvent& event : InTick.Events )
        {
            joined = joined || ( event.Name == "net.joined" && event.Payload == "{\"client\":1}" );
            killed = killed || ( event.Name == "kill" && event.Entity == net.ServerNet.GetNetId( *shooter.Get() ) );
            if( event.Name == "spawn" )
            {
                CHECK( InTick.Tick == spawnedAt );
                CHECK( event.Entity != 0u );
                CHECK( event.Entity == net.ServerNet.GetNetId( *spawned.Get() ) );
                spawnNamed = event.Entity != 0u;
            }
        }
        return true;
    } ) );
    MESSAGE( compared << " ticks compared with the live capture, " << commandsCompared << " commands, " << shotsCompared << " shots; "
                      << std::filesystem::file_size( path ) << " bytes" );
    CHECK( compared >= 300 );
    CHECK( commandsCompared >= 300 );
    CHECK( withRoundTrip + 30 >= commandsCompared );
    CHECK( shotsCompared >= 15 );
    CHECK( shotsCompared == shots.size() );
    CHECK( rewoundAway >= 5 );
    CHECK( moversCompared >= 300 );
    CHECK( joined );
    CHECK( killed );
    CHECK( spawnNamed );
    CHECK( names[net.ServerNet.GetNetId( *target.Get() )] == "Target" );
    CHECK( names[net.ServerNet.GetNetId( *shooter.Get() )] == "Shooter" );
    std::filesystem::remove( path );
}

TEST_CASE( "Replay: ReplayDump prints the summary, events, shots and command changes, filters by ticks and entity, and as JSON (ME-316)" )
{
    const std::filesystem::path path = TempFile( "Dump.mreplay" );
    std::filesystem::remove( path );
    {
        NetReplaySettings settings;
        settings.KeyframeInterval = 100;
        NetReplayRecorder recorder( MakeHeader(), settings );
        REQUIRE( recorder.OpenFile( path.string() ) );
        for( NetReplayTick& tick : MakeMatch( 0, 400 ) )
        {
            recorder.Record( std::move( tick ) );
        }
        recorder.Close();
    }
    NetReplayFileSource source;
    REQUIRE( source.Open( path.string() ) );
    NetReplayReader reader( source );
    auto dump = [&reader]( std::vector<std::string> InArgs ) {
        InArgs.insert( InArgs.begin(), "Player" );
        NetReplayDump::Options options;
        std::string error;
        REQUIRE_MESSAGE( NetReplayDump::ParseArgs( InArgs, options, error ), error );
        std::ostringstream out;
        REQUIRE_MESSAGE( NetReplayDump::Write( reader, options, out, error ), error );
        return out.str();
    };

    const std::string text = dump( { "--replay-dump", path.string() } );
    CHECK( text.find( "map          Assets/Scenes/Arena.lvl" ) != std::string::npos );
    CHECK( text.find( "ticks 0..399: 400 ticks" ) != std::string::npos );
    CHECK( text.find( "events kill x7 net.joined x1" ) != std::string::npos );
    CHECK( text.find( "shots 45: 23 hit" ) != std::string::npos );
    CHECK( text.find( "\n0 shot client 1 #3 HIT #4 (Head) at (0.000, 1.600, 9.600)" ) != std::string::npos );
    CHECK( text.find( "    hitbox #5 Body box" ) != std::string::npos );
    CHECK( text.find( "\n61 event kill {\"killer\":\"Alpha\"" ) != std::string::npos );
    // Client 1 holds its input for 20 ticks: its commands print when they change.
    CHECK( text.find( "\n20 command client 1 buttons 00000005" ) != std::string::npos );
    CHECK( text.find( "\n21 command client 1 " ) == std::string::npos );
    CHECK( text.find( "\n100 command client 1 " ) != std::string::npos );   // a new chunk doesn't reprint

    // A range and an entity: only what touches it, from inside a chunk (the names still come from its start).
    const std::string ranged = "\n" + dump( { "--replay-dump", path.string(), "--ticks", "150:199", "--shots", "--events" } );
    CHECK( ranged.find( "\n153 shot" ) != std::string::npos );
    CHECK( ranged.find( "\n144 shot" ) == std::string::npos );
    CHECK( ranged.find( "\n183 event kill" ) != std::string::npos );
    CHECK( ranged.find( "\n207 shot" ) == std::string::npos );

    const std::string states = dump( { "--replay-dump", path.string(), "--ticks", "100:101", "--entity", "65537", "--states" } );
    CHECK( states.find( "100 state Pickup#65537 at (3.000, 0.500, -2.000)" ) != std::string::npos );
    CHECK( states.find( " state #1 " ) == std::string::npos );
    // Characters' movers at full precision, with --states.
    const std::string movers = dump( { "--replay-dump", path.string(), "--ticks", "100:100", "--states" } );
    CHECK( movers.find( "100 mover #3 at (" ) != std::string::npos );
    CHECK( movers.find( "100 mover #5 at (" ) != std::string::npos );
    // A projectile's sweep names who fired it, and isn't counted with the shots.
    CHECK( text.find( "(sweep, fired by #3)" ) != std::string::npos );
    CHECK( text.find( "projectile sweeps 45: 0 hit" ) != std::string::npos );
    const std::string fired = dump( { "--replay-dump", path.string(), "--ticks", "9:9", "--shots", "--entity", "3" } );
    CHECK( fired.find( "fired by #3" ) != std::string::npos );

    const json document = json::parse( dump( { "--replay-dump", path.string(), "--json", "--summary", "--shots", "--ticks", ":20" } ) );
    CHECK( document["header"]["map"] == "Assets/Scenes/Arena.lvl" );
    CHECK( document["header"]["tickRate"] == 60 );
    CHECK( document["summary"]["ticks"] == 21 );
    CHECK( document["summary"]["shots"]["total"] == 3 );
    CHECK( document["summary"]["sweeps"]["total"] == 3 );
    CHECK( document["ticks"][0]["shots"][1]["instigator"] == 3 );
    REQUIRE( document["ticks"].size() == 3 );
    CHECK( document["ticks"][0]["tick"] == 0 );
    CHECK( document["ticks"][0]["shots"][0]["hit"] == true );
    CHECK( document["ticks"][0]["shots"][0]["hitboxes"].size() == 2 );

    NetReplayDump::Options options;
    std::string error;
    CHECK_FALSE( NetReplayDump::ParseArgs( { "Player", "--replay-dump" }, options, error ) );
    CHECK_FALSE( NetReplayDump::ParseArgs( { "Player", "--replay-dump", "x", "--ticks", "9:3" }, options, error ) );
    std::filesystem::remove( path );
}
