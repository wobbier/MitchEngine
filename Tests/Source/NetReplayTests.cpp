#include "PCH.h"
#include <doctest/doctest.h>
#include "Components/Networking/NetHitbox.h"
#include "Net/NetReplay.h"
#include "NetHarness.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <random>
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
            client.RoundTripMs = 20.f + id + static_cast<float>( InTick % 7 );
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
            CHECK( a.RoundTripMs == b.RoundTripMs );
            CHECK( a.PacketLoss == b.PacketLoss );
            CHECK( a.Slack == b.Slack );
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

TEST_CASE( "Replay: a server records every tick's capture, commands, joins and lag-compensated shots; the file at tick N is the live capture at tick N (ME-316)" )
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
    EntityHandle target;
    EntityHandle shooter;
    net.ServerWorld->ForEachEntity( [&]( Entity& InEntity ) {
        if( InEntity.GetGUID() == 0x7A26E7 ) target = net.ServerWorld->GetEntity( InEntity.GetId() );
        if( InEntity.GetGUID() == 0x5400 ) shooter = net.ServerWorld->GetEntity( InEntity.GetId() );
    } );
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
    net.ServerNet.SetCommandHandler( [&]( Entity& InShooter, const InputCommand& InCommand ) {
        commands[net.ServerNet.GetServer()->GetTick()] = InCommand;
        if( !( InCommand.Buttons & 2u ) )
        {
            return;
        }
        const EntityHandle self = net.ServerWorld->GetEntity( InShooter.GetId() );
        shots[net.ServerNet.GetServer()->GetTick()] = net.ServerNet.LagCompensatedRaycast( Vector3( 0.f, 1.f, -10.f ), Vector3( 0.f, 0.f, 1.f ), 100.f, InCommand, self );
    } );
    net.OnServerTick = [&]( NetServer& InServer ) {
        // The capture so far is the tick before's.
        if( InServer.GetTick() > 0 )
        {
            live[net.ServerNet.GetReplicationServer()->GetCurrent().Tick] = net.ServerNet.GetReplicationServer()->GetCurrent();
        }
        const float x = std::sin( static_cast<float>( InServer.GetTick() ) * 0.05f ) * 3.f;
        target->GetComponent<Transform>().SetPosition( Vector3( x, 1.f, 10.f ) );
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
    bool joined = false;
    bool killed = false;
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
        if( auto it = shots.find( InTick.Tick ); it != shots.end() )
        {
            REQUIRE( InTick.Shots.size() == 1 );
            const NetReplayShot& shot = InTick.Shots[0];
            CHECK( shot.Client == 1 );
            CHECK( shot.Shooter == net.ServerNet.GetNetId( *shooter.Get() ) );
            CHECK( shot.Hit == it->second.Hit );
            CHECK( shot.RewoundTicks == it->second.RewoundTicks );
            CHECK( shot.Target == ( it->second.Hit ? net.ServerNet.GetNetId( *target.Get() ) : 0u ) );
            // The target's hitbox, rewound: the shooter has none.
            REQUIRE( shot.Hitboxes.size() == 1 );
            CHECK( shot.Hitboxes[0].NetId == net.ServerNet.GetNetId( *target.Get() ) );
            CHECK( shot.Hitboxes[0].Name == "Target" );
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
    CHECK( joined );
    CHECK( killed );
    CHECK( names[net.ServerNet.GetNetId( *target.Get() )] == "Target" );
    CHECK( names[net.ServerNet.GetNetId( *shooter.Get() )] == "Shooter" );
    std::filesystem::remove( path );
}
