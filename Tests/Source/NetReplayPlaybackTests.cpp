#include "PCH.h"
#include <doctest/doctest.h>
#include "Components/Camera.h"
#include "Components/Networking/NetIdentity.h"
#include "Components/Networking/SingletonInput.h"
#include "Components/Transform.h"
#include "Engine/Input.h"
#include "Input/InputActions.h"
#include "Net/NetReplay.h"
#include "Net/NetReplayPlayback.h"
#include "Net/NetReplication.h"
#include "NetHarness.h"
#if USING( ME_SCRIPTING )
#include "Scripting/Bindings/BindingContext.h"
#include "Scripting/Bindings/Systems/Net.bindings.h"
#endif
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <random>
#include <string>
#include <vector>

// Match replay playback (ME-316 phase 2): a recorded session played back into a world through the
// client's replication path shows, at every tick N, exactly what the server captured at tick N.
namespace NetReplayPlaybackTest
{
    // Replicated fields of several kinds, changing every tick on the server.
    class Gauge
        : public Component<Gauge>
    {
        ME_REFLECTABLE( Gauge )
    public:
        Gauge() : Component( "NetReplayPlaybackTest::Gauge" ) {}
        int Count = 0;
        float Level = 0.f;
        std::string Tag;
        bool Lit = false;
    };

    // C# [Replicated] script bytes stand-in, keyed by NetId (a playback world's dynamic entities
    // aren't the server's GUIDs): the server writes netId * 1000 + tick % 97; the viewer keeps
    // what it read and writes it back when the world is captured.
    struct Scripts
    {
        std::map<uint32_t, std::vector<uint8_t>> Read;
        const NetCore* Server = nullptr;

        NetScriptState State()
        {
            NetScriptState state;
            state.WriteReplicated = [this]( Entity& InEntity, std::vector<uint8_t>& OutBytes ) {
                const uint32_t id = InEntity.GetComponent<NetIdentity>().GetNetId();
                if( Server )
                {
                    const int32_t value = static_cast<int32_t>( id * 1000u + const_cast<NetCore*>( Server )->GetServer()->GetTick() % 97u );
                    uint8_t bytes[4];
                    std::memcpy( bytes, &value, 4 );
                    OutBytes.insert( OutBytes.end(), bytes, bytes + 4 );
                }
                else if( auto it = Read.find( id ); it != Read.end() )
                {
                    OutBytes.insert( OutBytes.end(), it->second.begin(), it->second.end() );
                }
            };
            state.ReadReplicated = [this]( Entity& InEntity, const std::vector<uint8_t>& InBytes, bool ) {
                Read[InEntity.GetComponent<NetIdentity>().GetNetId()] = InBytes;
            };
            return state;
        }
    };

    std::filesystem::path TempFile( const char* InName )
    {
        const std::filesystem::path folder = std::filesystem::temp_directory_path() / "MitchReplayTests";
        std::filesystem::create_directories( folder );
        return folder / InName;
    }

    EntityHandle Find( World& InWorld, uint64_t InGUID )
    {
        EntityHandle found;
        InWorld.ForEachEntity( [&]( Entity& InEntity ) {
            if( InEntity.GetGUID() == InGUID )
            {
                found = InWorld.GetEntity( InEntity.GetId() );
            }
        } );
        return found;
    }

    constexpr uint64_t kSpinner = 0x5917;   // moves, turns, its fields change; a client takes it over
    constexpr uint64_t kStill = 0x5711;     // never moves
    constexpr uint64_t kDoomed = 0xD00D;    // placed, destroyed near the end

    // The scene, the same in the server's world and the viewer's (the static NetIds match).
    void PlaceScene( World& InWorld )
    {
        for( const uint64_t guid : { kSpinner, kStill, kDoomed } )
        {
            EntityHandle entity = InWorld.CreateEntityWithGUID( guid );
            entity->AddComponent<Transform>().SetPosition( Vector3( static_cast<float>( guid % 7 ), 0.f, static_cast<float>( guid % 5 ) ) );
            entity->AddComponent<NetIdentity>();
            if( guid == kSpinner )
            {
                entity->AddComponent<Gauge>();
            }
        }
    }

    // A recorded session: the server's live capture at every tick, and the file.
    struct Recording
    {
        std::map<uint32_t, NetSnapshot> Live;
        std::filesystem::path Path;
        uint32_t DoomedAt = 0;      // the tick the placed entity was destroyed
        uint32_t SpawnedAt = 0;
        uint32_t DespawnedAt = 0;
        uint32_t OwnedAt = 0;
    };

    Recording Record( const char* InName )
    {
        using namespace NetHarness;
        NetConditions link;
        link.LatencyMs = 30.f;
        link.Seed = 3;
        Harness net( link, link );
        PlaceScene( *net.ServerWorld );
        PlaceScene( *net.ClientWorld );
        Scripts serverScripts;
        serverScripts.Server = &net.ServerNet;
        NetCore::ScriptGlue glue;
        glue.State = serverScripts.State();
        net.ServerNet.SetScriptGlue( glue );
        net.ServerWorld->Simulate();
        net.ServerWorld->Start();

        Recording recording;
        recording.Path = TempFile( InName );
        std::filesystem::remove( recording.Path );
        EntityHandle spinner = Find( *net.ServerWorld, kSpinner );
        EntityHandle doomed = Find( *net.ServerWorld, kDoomed );
        EntityHandle visitor;     // spawned, then destroyed
        EntityHandle stayer;      // spawned, stays
        net.OnServerTick = [&]( NetServer& InServer ) {
            NetReplicationServer& replication = *net.ServerNet.GetReplicationServer();
            if( net.ServerNet.IsRecording() && net.ServerNet.GetRecorder()->GetTicksRecorded() > 0 )
            {
                recording.Live[replication.GetCurrent().Tick] = replication.GetCurrent();
            }
            const uint32_t tick = InServer.GetTick();
            const float t = static_cast<float>( tick ) / 60.f;
            Transform& transform = spinner->GetComponent<Transform>();
            // Off the 1/1024 m grid on purpose: the capture quantizes, playback must show the quantized pose.
            transform.SetPosition( Vector3( std::sin( t ) * 5.00003f, 1.f + 0.25f * std::sin( t * 3.1f ), std::cos( t * 0.7f ) * 3.3333f ) );
            transform.SetRotation( Quaternion::AngleAxis( t * 1.7f, Vector3( 0.3f, 1.f, 0.1f ).Normalized() ) );
            Gauge& gauge = spinner->GetComponent<Gauge>();
            gauge.Count = static_cast<int>( tick / 7 );
            gauge.Level = std::sin( t * 2.f );
            gauge.Tag = "tick " + std::to_string( tick / 30 );
            gauge.Lit = ( tick / 45 ) % 2 == 0;
            if( net.ServerNet.IsRecording() )
            {
                if( !visitor && !recording.SpawnedAt && tick >= 130 )
                {
                    visitor = net.ServerWorld->CreateEntity();
                    visitor->SetName( "Visitor" );
                    visitor->AddComponent<Transform>().SetPosition( Vector3( 2.f, 0.f, 2.f ) );
                    visitor->AddComponent<NetIdentity>();
                    recording.SpawnedAt = tick;
                }
                if( visitor )
                {
                    visitor->GetComponent<Transform>().SetPosition( Vector3( 2.f + t, 0.f, 2.f ) );
                }
                if( visitor && tick >= 250 )
                {
                    net.ServerWorld->DestroyEntity( visitor );
                    visitor = EntityHandle();
                    recording.DespawnedAt = tick;
                }
                if( !stayer && tick >= 170 )
                {
                    stayer = net.ServerWorld->CreateEntity();
                    stayer->SetName( "Stayer" );
                    stayer->AddComponent<Transform>().SetPosition( Vector3( -2.f, 0.f, -2.f ) );
                    // No Gauge: spawned without a prefab, a client makes it a bare entity.
                    stayer->AddComponent<NetIdentity>().SetOwner( 1 );
                }
                if( !recording.OwnedAt && tick >= 200 )
                {
                    spinner->GetComponent<NetIdentity>().SetOwner( 1 );
                    recording.OwnedAt = tick;
                }
                if( doomed && tick >= 400 )
                {
                    net.ServerWorld->DestroyEntity( doomed );
                    doomed = EntityHandle();
                    recording.DoomedAt = tick;
                }
            }
            net.ServerWorld->Simulate();
        };
        net.Start();
        net.Run( 0.5 );
        NetReplaySettings settings;
        settings.KeyframeInterval = 60;
        REQUIRE( net.ServerNet.StartRecording( recording.Path.string(), NetReplayHeader(), settings ) );
        net.Run( 7.5 );
        net.ServerNet.StopRecording();
        REQUIRE( recording.DoomedAt > 0 );
        return recording;
    }

    // The viewer: a world with the same scene, a session-less replication client and the playback.
    struct Viewer
    {
        std::shared_ptr<World> GameWorld = std::make_shared<World>();
        Scripts Script;
        NetReplicationClient Client{ *GameWorld };
        NetReplayFileSource Source;
        std::unique_ptr<NetReplayPlayback> Playback;

        explicit Viewer( const std::filesystem::path& InPath )
        {
            GameWorld->IsLoading = false;
            PlaceScene( *GameWorld );
            Client.Scripts = Script.State();
            Client.AssignStaticIds();
            REQUIRE_MESSAGE( Source.Open( InPath.string() ), Source.GetError() );
            Playback = std::make_unique<NetReplayPlayback>( Source, Client );
        }

        // A frame: the playhead, then the client poses the world.
        void Frame( double InSeconds = 0.0 )
        {
            REQUIRE_MESSAGE( Playback->Update( InSeconds ), Playback->GetError() );
            Client.Update( InSeconds );
            // The engine's sync point: destroyed entities go.
            GameWorld->Simulate();
        }

        // The world at the playhead (a whole tick) is the live capture there, bit for bit.
        void ExpectLive( const Recording& InRecording )
        {
            const uint32_t tick = Playback->GetTick();
            INFO( "tick " << tick );
            auto live = InRecording.Live.find( tick );
            REQUIRE( live != InRecording.Live.end() );
            const NetReplayPlayback::Check check = Playback->CheckWorld( &Client.Scripts );
            for( const std::string& problem : check.Problems )
            {
                FAIL_CHECK( problem );
            }
            CHECK( check.Passed() );
            CHECK( check.Entities == live->second.Entities.size() );
            // Independently of the recording: what the client shows and the world re-captured are the
            // server's own capture at this tick.
            CHECK( Client.GetEntities().size() == live->second.Entities.size() );
            for( const auto& [id, state] : live->second.Entities )
            {
                INFO( "NetId " << id );
                const NetEntityState* shown = Client.GetShownState( id );
                REQUIRE( shown );
                CHECK( *shown == state );
                EntityHandle entity = Client.FindEntity( id );
                REQUIRE( entity );
                const NetEntityState captured = NetReplicationState::Capture( *entity.Get(), &Client.Scripts );
                CHECK( captured.Position == state.Position );
                CHECK( captured.Fields == state.Fields );
                CHECK( Quaternion::Angle( captured.GetRotation(), state.GetRotation() ) < 2e-3f );
            }
            // Owners: the spinner is the server's until OwnedAt, client 1's after (seeking back across
            // OwnedAt hands it back).
            if( EntityHandle spinner = Client.FindEntity( 2 ); spinner && tick != InRecording.OwnedAt )
            {
                CHECK( spinner->GetComponent<NetIdentity>().GetOwner() == ( tick > InRecording.OwnedAt ? 1u : 0u ) );
            }
        }
    };
}

ME_REFLECT_BEGIN( NetReplayPlaybackTest::Gauge )
    ME_FIELD( Count ).Replicated();
    ME_FIELD( Level ).Replicated();
    ME_FIELD( Tag ).Replicated();
    ME_FIELD( Lit ).Replicated();
ME_REFLECT_END()
ME_REGISTER_COMPONENT( NetReplayPlaybackTest::Gauge )

using namespace NetReplayPlaybackTest;

TEST_CASE( "Replay playback: stepping through a recorded session, the world at every tick N is the server's live capture at tick N, bit for bit (ME-316)" )
{
    const Recording recording = Record( "Playback.mreplay" );
    Viewer viewer( recording.Path );
    NetReplayPlayback& playback = *viewer.Playback;
    REQUIRE_FALSE( playback.IsEmpty() );
    CHECK( playback.IsPaused() );
    CHECK( playback.GetFirstTick() == recording.Live.begin()->first );
    CHECK( playback.GetLastTick() >= recording.Live.rbegin()->first );

    // Every tick, one step at a time: deltas fed one after another, one rebuild (the first tick).
    size_t compared = 0;
    bool sawVisitor = false;
    bool sawOwner = false;
    viewer.Frame();
    for( uint32_t tick = playback.GetFirstTick(); tick <= recording.Live.rbegin()->first; ++tick )
    {
        if( tick > playback.GetFirstTick() )
        {
            playback.Step( 1 );
            viewer.Frame();
        }
        REQUIRE( playback.GetTick() == tick );
        viewer.ExpectLive( recording );
        ++compared;
        sawVisitor = sawVisitor || ( tick > recording.SpawnedAt && tick < recording.DespawnedAt && viewer.Client.GetEntities().size() == 5 );
        // Static NetIds go by GUID: Still 1, Spinner 2, Doomed 3.
        if( EntityHandle spinner = viewer.Client.FindEntity( 2 ); spinner && tick > recording.OwnedAt )
        {
            sawOwner = sawOwner || spinner->GetComponent<NetIdentity>().GetOwner() == 1;
        }
    }
    MESSAGE( compared << " ticks played back and compared; " << playback.GetTicksFed() << " fed, " << playback.GetRebuilds() << " rebuild(s)" );
    CHECK( compared >= 400 );
    CHECK( playback.GetRebuilds() == 1 );
    CHECK( sawVisitor );
    CHECK( sawOwner );
    // The placed entity destroyed at DoomedAt is gone from the viewer's world too.
    CHECK_FALSE( viewer.Client.FindEntity( 3 ) );
    CHECK_FALSE( Find( *viewer.GameWorld, kDoomed ) );

    // Played at 0.3x (between ticks almost every frame) from the start to the end: each chunk
    // is read and decoded once, not again at every chunk boundary.
    REQUIRE( playback.GetReader().GetChunkCount() >= 2 );
    playback.Seek( playback.GetFirstTick() );
    viewer.Frame();
    const uint64_t loads = playback.GetReader().GetChunkLoads();
    playback.SetSpeed( 0.3 );
    playback.Play();
    for( int frame = 0; frame < 10000 && !playback.IsPaused(); ++frame )
    {
        viewer.Frame( 1.0 / 60.0 );
    }
    CHECK( playback.GetTick() == playback.GetLastTick() );
    CHECK( playback.GetReader().GetChunkLoads() - loads == playback.GetReader().GetChunkCount() - 1 );
}

TEST_CASE( "Replay playback: seeking anywhere (back, forward, across keyframes, a spawn and a despawn) shows that tick exactly; step, speed, loop and pause-at (ME-316)" )
{
    const Recording recording = Record( "PlaybackSeek.mreplay" );
    Viewer viewer( recording.Path );
    NetReplayPlayback& playback = *viewer.Playback;
    const uint32_t first = playback.GetFirstTick();

    // Seeks in a shuffled order, all before the placed entity's destruction (nothing brings a
    // destroyed placed entity back, as on a client): each rebuilds from its chunk's keyframe or
    // catches up from the tick before.
    std::vector<uint32_t> ticks;
    for( uint32_t tick = first; tick < recording.DoomedAt; tick += 7 )
    {
        ticks.push_back( tick );
    }
    ticks.push_back( recording.SpawnedAt );
    ticks.push_back( recording.SpawnedAt + 1 );
    ticks.push_back( recording.DespawnedAt - 1 );
    ticks.push_back( recording.DespawnedAt );
    std::mt19937 random( 316 );
    std::shuffle( ticks.begin(), ticks.end(), random );
    for( const uint32_t tick : ticks )
    {
        playback.Seek( tick );
        viewer.Frame();
        REQUIRE( playback.GetTick() == tick );
        viewer.ExpectLive( recording );
    }
    CHECK( playback.GetRebuilds() > ticks.size() / 3 );

    // Step back and forth.
    playback.Seek( 300 );
    viewer.Frame();
    playback.Step( -1 );
    viewer.Frame();
    CHECK( playback.GetTick() == 299 );
    viewer.ExpectLive( recording );
    playback.Step( 10 );
    viewer.Frame();
    CHECK( playback.GetTick() == 309 );
    CHECK( playback.IsPaused() );
    viewer.ExpectLive( recording );

    // Speed: clamped to 0.1x-4x. Half speed shows the tick between two, interpolated.
    playback.SetSpeed( 10.0 );
    CHECK( playback.GetSpeed() == doctest::Approx( NetReplayPlayback::kMaxSpeed ) );
    playback.SetSpeed( 0.0 );
    CHECK( playback.GetSpeed() == doctest::Approx( NetReplayPlayback::kMinSpeed ) );
    playback.SetSpeed( 0.5 );
    playback.Seek( 320 );
    playback.Play();
    viewer.Frame( 1.0 / 60.0 );
    CHECK( playback.GetTime() == doctest::Approx( 320.5 ) );
    {
        EntityHandle spinner = viewer.Client.FindEntity( 2 );
        const Vector3 a = recording.Live.at( 320 ).Entities.at( 2 ).GetPosition();
        const Vector3 b = recording.Live.at( 321 ).Entities.at( 2 ).GetPosition();
        CHECK( ( a - b ).Length() > 0.01f );
        const Vector3 shown = spinner->GetComponent<Transform>().GetWorldPosition();
        CHECK( ( shown - ( a + b ) * 0.5f ).Length() < 1e-4f );
    }
    // 4x: four ticks a frame, fed one after another (no rebuild).
    playback.SetSpeed( 4.0 );
    const uint64_t rebuilds = playback.GetRebuilds();
    playback.Seek( 330 );
    viewer.Frame();
    for( int frame = 0; frame < 10; ++frame )
    {
        viewer.Frame( 1.0 / 60.0 );
    }
    CHECK( playback.GetTime() == doctest::Approx( 370.0 ) );
    CHECK( playback.GetRebuilds() == rebuilds );    // the seek to 330 caught up from 321

    // Pause-at: playing forward stops exactly there, on a whole tick that checks out.
    playback.SetPauseAt( 383 );
    for( int frame = 0; frame < 10 && !playback.IsPaused(); ++frame )
    {
        viewer.Frame( 1.0 / 60.0 );
    }
    CHECK( playback.IsPaused() );
    CHECK( playback.GetTime() == 383.0 );
    viewer.ExpectLive( recording );

    // A loop: playing past its end goes back to its start, again and again.
    playback.SetSpeed( 4.0 );
    playback.SetLoop( 150, 170 );
    playback.Seek( 150 );
    playback.Play();
    double highest = 0.0;
    for( int frame = 0; frame < 40; ++frame )
    {
        viewer.Frame( 1.0 / 60.0 );
        CHECK( playback.GetTime() >= 150.0 );
        CHECK( playback.GetTime() <= 170.0 );
        highest = std::max( highest, playback.GetTime() );
    }
    CHECK( highest >= 166.0 );
    playback.ClearLoop();
    playback.Pause();
    playback.Seek( 160 );
    viewer.Frame();
    viewer.ExpectLive( recording );
    // A world whose owner isn't the recording's fails the check (and says whose).
    {
        NetIdentity& identity = viewer.Client.FindEntity( 2 )->GetComponent<NetIdentity>();
        REQUIRE( identity.GetOwner() == 0 );
        identity.SetOwner( 1 );
        const NetReplayPlayback::Check wrong = playback.CheckWorld( &viewer.Client.Scripts );
        CHECK_FALSE( wrong.Passed() );
        REQUIRE( wrong.Problems.size() == 1 );
        CHECK( wrong.Problems[0].find( "owner 1, recorded 0" ) != std::string::npos );
        identity.SetOwner( 0 );
    }

    // Past the end: it stops at the last tick.
    playback.Seek( playback.GetLastTick() - 2 );
    playback.Play();
    for( int frame = 0; frame < 5; ++frame )
    {
        viewer.Frame( 1.0 / 60.0 );
    }
    CHECK( playback.IsPaused() );
    CHECK( playback.GetTick() == playback.GetLastTick() );
    // A pause-at past the end rests on the last tick (and is done with); a negative one is none.
    playback.SetPauseAt( -5 );
    CHECK( playback.GetPauseAt() == -1 );
    playback.SetPauseAt( static_cast<int64_t>( playback.GetLastTick() ) + 100 );
    CHECK( playback.GetPauseAt() == playback.GetLastTick() );
    playback.Seek( playback.GetLastTick() - 2 );
    playback.Play();
    for( int frame = 0; frame < 5; ++frame )
    {
        viewer.Frame( 1.0 / 60.0 );
    }
    CHECK( playback.IsPaused() );
    CHECK( playback.GetTick() == playback.GetLastTick() );
    CHECK( playback.GetPauseAt() < 0 );

    // Seeking back before the placed entity's destruction can't bring it back: the check says so.
    playback.Seek( recording.DoomedAt - 5 );
    viewer.Frame();
    const NetReplayPlayback::Check check = playback.CheckWorld( &viewer.Client.Scripts );
    CHECK_FALSE( check.Passed() );
    REQUIRE( check.Problems.size() == 1 );
    CHECK( check.Problems[0].find( "not in the world" ) != std::string::npos );
}

TEST_CASE( "Replay playback: NetCore plays a replay with no session: a client that presents (no prediction, networked scripts don't simulate), the frame hooks move the playhead, another scene is refused (ME-316)" )
{
    const Recording recording = Record( "PlaybackCore.mreplay" );
    auto open = [&recording]() {
        auto source = std::make_unique<NetReplayFileSource>();
        REQUIRE_MESSAGE( source->Open( recording.Path.string() ), source->GetError() );
        return source;
    };

    // Another scene: its placed NetIds wouldn't line up.
    {
        NetCore other;
        auto world = std::make_shared<World>();
        world->IsLoading = false;
        world->AddCore( other );
        EntityHandle lone = world->CreateEntityWithGUID( kStill );
        lone->AddComponent<Transform>();
        lone->AddComponent<NetIdentity>();
        std::string error;
        CHECK_FALSE( other.StartPlayback( open(), &error ) );
        CHECK( error.find( "another scene" ) != std::string::npos );
        CHECK_FALSE( other.IsActive() );
    }

    Scripts script;
    NetCore net;    // before the world
    auto world = std::make_shared<World>();
    world->IsLoading = false;
    world->AddCore( net );
    PlaceScene( *world );
    EntityHandle prop = world->CreateEntity();     // not networked: simulates as ever
    prop->AddComponent<Transform>();
    NetCore::ScriptGlue glue;
    glue.State = script.State();
    net.SetScriptGlue( glue );
    std::string error;
    REQUIRE_MESSAGE( net.StartPlayback( open(), &error ), error );
    CHECK_FALSE( net.StartPlayback( open(), &error ) );     // one at a time
    CHECK( net.IsActive() );
    CHECK( net.IsPlayback() );
    CHECK( net.IsClient() );
    CHECK_FALSE( net.IsServer() );
    CHECK_FALSE( net.IsSessionLive() );     // nothing to hold script reloads for
    CHECK_FALSE( net.GetClient() );
    CHECK_FALSE( net.StartRecording( ( recording.Path.string() + ".again" ) ) );
    NetReplayPlayback& playback = *net.GetPlayback();
    const uint32_t first = playback.GetFirstTick();
    CHECK( playback.IsPaused() );

    // Frames the way Engine::Run drives NetCore, playing to a pause-at tick.
    playback.SetPauseAt( first + 300 );
    playback.Play();
    double now = 0.0;
    for( int frame = 0; frame < 400 && !playback.IsPaused(); ++frame )
    {
        now += 1.0 / 60.0;
        net.Receive( now );
        net.BeginTick();
        net.EndTick( now );
        net.WriteInterpolated( 1.0 / 60.0 );
        net.Flush( now );
        world->Simulate();
    }
    CHECK( playback.IsPaused() );
    CHECK( net.GetTick() == first + 300 );
    const NetReplayPlayback::Check check = net.CheckPlayback();
    for( const std::string& problem : check.Problems )
    {
        FAIL_CHECK( problem );
    }
    CHECK( check.Passed() );
    CHECK( check.Matched == recording.Live.at( first + 300 ).Entities.size() );

    // Presentation: nothing predicted, networked entities don't simulate, the rest does; the
    // entity the recording gave client 1 is its owner here too.
    EntityHandle spinner = net.FindEntity( 2 );
    REQUIRE( spinner );
    CHECK( spinner->GetComponent<NetIdentity>().GetOwner() == 1 );
    CHECK_FALSE( net.IsPredicted( *spinner.Get() ) );
    CHECK_FALSE( net.GetPredictor() );
    CHECK_FALSE( net.SimulatesLocally( *spinner.Get() ) );
    CHECK( net.SimulatesLocally( *prop.Get() ) );
    InputCommand command;
    InputCommand previous;
    CHECK_FALSE( net.GetCommand( *spinner.Get(), command, previous ) );

    // The report says it's a replay.
    const std::filesystem::path report = TempFile( "PlaybackReport.json" );
    REQUIRE( net.WriteReport( report.string(), now ) );
    {
        std::ifstream in( report );
        const std::string text( ( std::istreambuf_iterator<char>( in ) ), std::istreambuf_iterator<char>() );
        CHECK( text.find( "\"replay\"" ) != std::string::npos );
    }
    net.Shutdown();
    CHECK_FALSE( net.IsActive() );
    CHECK_FALSE( net.IsClient() );
}


TEST_CASE( "Replay inspector: NetSchema::Describe reads recorded field bytes by name against an entity's components, without applying them (ME-316)" )
{
    auto world = std::make_shared<World>();
    world->IsLoading = false;
    EntityHandle entity = world->CreateEntity();
    entity->AddComponent<Transform>();
    Gauge& gauge = entity->AddComponent<Gauge>();
    gauge.Count = -7;
    gauge.Level = 2.5f;
    gauge.Tag = "red";
    gauge.Lit = true;
    std::vector<uint8_t> bytes = NetSchema::Write( *entity.Get() );
    const size_t schemaBytes = bytes.size();
    // A script's [Replicated] bytes follow in a snapshot's state.
    bytes.push_back( 0xAB );
    bytes.push_back( 0xCD );
    gauge.Count = 0;
    gauge.Tag = "blue";

    size_t used = 0;
    const std::vector<NetSchema::FieldValue> values = NetSchema::Describe( *entity.Get(), bytes, &used );
    REQUIRE( values.size() == 4 );
    CHECK( values[0].Component == "NetReplayPlaybackTest::Gauge" );
    CHECK( values[0].Field == "Count" );
    CHECK( values[0].Value == "-7" );
    CHECK( values[1].Field == "Level" );
    CHECK( values[1].Value == "2.5000" );
    CHECK( values[2].Value == "\"red\"" );
    CHECK( values[3].Value == "true" );
    CHECK( used == schemaBytes );
    // Nothing applied.
    CHECK( gauge.Count == 0 );
    CHECK( gauge.Tag == "blue" );
    CHECK( NetSchema::Describe( *entity.Get(), {}, &used ).empty() );
    CHECK( used == 0 );
}


TEST_CASE( "Replay inspector: a recorded state's Fields unpack into its NetSchema bytes, read by name, and its script bytes (ME-316)" )
{
    auto world = std::make_shared<World>();
    world->IsLoading = false;
    EntityHandle entity = world->CreateEntity();
    entity->AddComponent<Transform>();
    Gauge& gauge = entity->AddComponent<Gauge>();
    gauge.Count = 300;      // a varuint wider than a byte: the u16 size read as a field would show
    gauge.Level = -1.25f;
    gauge.Tag = "green";
    gauge.Lit = true;
    // A script writing [Replicated] bytes after the C++ ones: the state a snapshot (and a replay) records.
    NetScriptState scripts;
    scripts.WriteReplicated = []( Entity&, std::vector<uint8_t>& OutBytes ) {
        OutBytes.push_back( 0xAB );
        OutBytes.push_back( 0xCD );
    };
    const NetEntityState state = NetReplicationState::Capture( *entity.Get(), &scripts );
    const std::vector<uint8_t> schema = NetSchema::Write( *entity.Get() );
    REQUIRE( state.Fields.size() == 2 + schema.size() + 2 );

    std::vector<uint8_t> reflected;
    std::vector<uint8_t> script;
    const size_t start = NetReplicationState::Unpack( state.Fields, reflected, script );
    CHECK( reflected == schema );
    CHECK( script == std::vector<uint8_t>{ 0xAB, 0xCD } );
    CHECK( start == 2 + schema.size() );
    const std::vector<NetSchema::FieldValue> values = NetSchema::Describe( *entity.Get(), reflected );
    REQUIRE( values.size() == 4 );
    CHECK( values[0].Value == "300" );
    CHECK( values[1].Value == "-1.2500" );
    CHECK( values[2].Value == "\"green\"" );
    CHECK( values[3].Value == "true" );
    // The packed bytes as they are aren't the fields: the size comes first.
    const std::vector<NetSchema::FieldValue> packed = NetSchema::Describe( *entity.Get(), state.Fields );
    CHECK( ( packed.empty() || packed[0].Value != "300" ) );

    // No C++ fields (HeroShooter's entities): the script bytes start after the size.
    EntityHandle scripted = world->CreateEntity();
    scripted->AddComponent<Transform>();
    const NetEntityState scriptOnly = NetReplicationState::Capture( *scripted.Get(), &scripts );
    CHECK( NetReplicationState::Unpack( scriptOnly.Fields, reflected, script ) == 2 );
    CHECK( reflected.empty() );
    CHECK( script == std::vector<uint8_t>{ 0xAB, 0xCD } );
    CHECK( NetReplicationState::Unpack( {}, reflected, script ) == 0 );
    CHECK( script.empty() );
}


// ME-297 (NET-16): per-world state lives in world singletons, so a second world (a killcam's
// replay world) runs beside the game's without either reading the other's.
TEST_CASE( "World singletons: a live client world and a replay world tick side by side in one process, each with its own NetCore, input and main camera; neither reads the other's (ME-297)" )
{
    using namespace NetHarness;
    const Recording recording = Record( "TwoWorlds.mreplay" );

    // The live session: a server world and a client world. Its server holds the spinner far from
    // anywhere the recording has it.
    NetConditions link;
    link.LatencyMs = 20.f;
    link.Seed = 5;
    Harness live( link, link );
    PlaceScene( *live.ServerWorld );
    PlaceScene( *live.ClientWorld );
    live.ServerWorld->Simulate();
    live.ServerWorld->Start();
    const Vector3 liveSpot( 100.f, 0.f, 0.f );
    EntityHandle liveServerSpinner = Find( *live.ServerWorld, kSpinner );
    live.OnServerTick = [&]( NetServer& ) { liveServerSpinner->GetComponent<Transform>().SetPosition( liveSpot ); };

    // The replay world, a killcam's: its own NetCore playing the recording (no session).
    NetCore replayNet;    // before the world
    auto replayWorld = std::make_shared<World>();
    replayWorld->IsLoading = false;
    replayWorld->AddCore( replayNet );
    PlaceScene( *replayWorld );
    Scripts replayScripts;      // the recording carries [Replicated] script bytes
    NetCore::ScriptGlue glue;
    glue.State = replayScripts.State();
    replayNet.SetScriptGlue( glue );
    auto source = std::make_unique<NetReplayFileSource>();
    REQUIRE_MESSAGE( source->Open( recording.Path.string() ), source->GetError() );
    std::string error;
    REQUIRE_MESSAGE( replayNet.StartPlayback( std::move( source ), &error ), error );
    NetReplayPlayback& playback = *replayNet.GetPlayback();
    const uint32_t pauseAt = playback.GetFirstTick() + 240;
    playback.SetPauseAt( pauseAt );
    playback.Play();

    // Each world's input (SingletonInput) from its own action map: different layouts.
    auto mapOf = []( const std::vector<std::string>& InNetworked ) {
        InputContext gameplay;
        gameplay.Name = "Gameplay";
        for( const std::string& name : InNetworked )
        {
            InputAction action;
            action.Name = name;
            action.Type = InputActionType::Button;
            action.Networked = true;
            gameplay.Actions.push_back( action );
        }
        InputActionMap map;
        map.Contexts = { gameplay };
        return map;
    };
    Input liveDevices;
    liveDevices.GetActions().SetMap( mapOf( { "Fire" } ) );
    Input replayDevices;
    replayDevices.GetActions().SetMap( mapOf( { "Ability", "Fire" } ) );
    REQUIRE( live.ClientWorld->FindSingleton<SingletonInput>() == nullptr );
    live.ClientWorld->Singleton<SingletonInput>().BuildCommandLayout( liveDevices );
    replayWorld->Singleton<SingletonInput>().BuildCommandLayout( replayDevices );
    SingletonInput& liveInput = live.ClientWorld->Singleton<SingletonInput>();
    SingletonInput& replayInput = replayWorld->Singleton<SingletonInput>();
    REQUIRE( &liveInput != &replayInput );
    CHECK( liveInput.GetLayoutVersion() != replayInput.GetLayoutVersion() );
    CHECK( liveInput.GetActionIndex( "Fire" ) == 0 );
    CHECK( replayInput.GetActionIndex( "Fire" ) == 1 );
    CHECK( liveInput.GetCommandSchema() != replayInput.GetCommandSchema() );
    // A bot holds Fire in the live world (Net.Press): the replay world's input never sees it.
    liveInput.SetActionOverride( liveInput.GetActionIndex( "Fire" ), true );

    // The live client samples its world's input every tick, the way Engine::Run does.
    live.MakeCommand = [&]( uint32_t ) {
        SingletonInput& input = live.ClientWorld->Singleton<SingletonInput>();
        InputCommand command;
        input.SampleCommand( liveDevices, command );
        input.RecordLocalCommand( command );
        return command;
    };
    // The replay world ticks on its own 60 Hz frames, interleaved with the live session's in the
    // same loop.
    int replayFrames = 0;
    live.OnMillisecond = [&]( double InNow ) {
        if( static_cast<int64_t>( InNow * 60.0 ) == static_cast<int64_t>( ( InNow - 0.001 ) * 60.0 ) )
        {
            return;
        }
        replayNet.Receive( InNow );
        replayNet.BeginTick();
        replayNet.EndTick( InNow );
        replayNet.WriteInterpolated( 1.0 / 60.0 );
        replayNet.Flush( InNow );
        replayWorld->Simulate();
        ++replayFrames;
    };

    // Each world's main camera (SingletonCamera).
    auto addCamera = []( World& InWorld, const char* InName ) -> EntityHandle {
        EntityHandle entity = InWorld.CreateEntity( InName );
        entity->AddComponent<Transform>();
        entity->AddComponent<Camera>();
        return entity;
    };
    EntityHandle liveCamera = addCamera( *live.ClientWorld, "Live Camera" );
    EntityHandle killcam = addCamera( *replayWorld, "Killcam" );
    killcam->GetComponent<Camera>().SetCurrent();

    live.Start();
    live.Run( 6.0 );

    // Both ran.
    REQUIRE( live.ClientNet.GetClient()->GetState() == NetClient::State::Connected );
    CHECK( replayFrames >= 350 );
    REQUIRE( playback.IsPaused() );
    CHECK( replayNet.GetTick() == pauseAt );
    CHECK( live.ClientNet.GetTick() != replayNet.GetTick() );

    // The replay world is the recording at its tick, untouched by the live session beside it.
    const NetReplayPlayback::Check check = replayNet.CheckPlayback();
    for( const std::string& problem : check.Problems )
    {
        FAIL_CHECK( problem );
    }
    CHECK( check.Passed() );
    CHECK( check.Matched == recording.Live.at( pauseAt ).Entities.size() );
    const Vector3 replayed = Find( *replayWorld, kSpinner )->GetComponent<Transform>().GetPosition();
    CHECK( ( replayed - liveSpot ).Length() > 50.f );
    // The live client shows its own server's world, not the replay's.
    const Vector3 shown = Find( *live.ClientWorld, kSpinner )->GetComponent<Transform>().GetPosition();
    CHECK( ( shown - liveSpot ).Length() < 0.01f );

    // Input: the live world sampled every tick with Fire held; the replay world sampled nothing.
    REQUIRE( liveInput.HasLocalCommand() );
    CHECK( liveInput.GetLocalCommand().Buttons == 1u );
    CHECK( liveInput.GetLocalCommand().Tick > 300u );
    CHECK_FALSE( replayInput.HasLocalCommand() );
    CHECK( replayInput.HasCommandLayout() );     // its layout stands, its own
    CHECK( replayInput.GetActionIndex( "Ability" ) == 0 );

    // Cameras: each world's main camera is its own.
    CHECK( Camera::GetMain( live.ClientWorld.get() ) == &liveCamera->GetComponent<Camera>() );
    CHECK( Camera::GetMain( replayWorld.get() ) == &killcam->GetComponent<Camera>() );
    CHECK( liveCamera->GetComponent<Camera>().IsMain() );

#if USING( ME_SCRIPTING )
    // C#'s Net reads its script world's: role, tick, layout, the local command, held actions.
    ScriptEngineAPI api{};
    Register_NetBindings( api );
    const WeakPtr<World> previousWorld = ScriptBindings::GetWorld();
    const uint8_t* fire = reinterpret_cast<const uint8_t*>( "Fire" );
    int buttons = 0;
    int previousButtons = 0;
    float axes[4] = {};
    float aim[2] = {};
    double viewTick = 0.0;
    int tick = 0;
    {
        ScriptBindings::WorldScope scope( live.ClientWorld );
        CHECK( api.Net_GetRole() == 2 );
        CHECK( api.Net_GetTick() == static_cast<int>( live.ClientNet.GetTick() ) );
        CHECK( api.Net_GetActionIndex( fire ) == 0 );
        CHECK( api.Net_GetLayoutVersion() == static_cast<int>( liveInput.GetLayoutVersion() ) );
        CHECK( ( api.Net_GetLocalCommand( &buttons, &previousButtons, axes, aim, &viewTick, &tick ) & 1 ) == 1 );
        CHECK( buttons == 1 );
    }
    {
        ScriptBindings::WorldScope scope( replayWorld );
        CHECK( api.Net_GetRole() == 2 );    // a replay presents like a client
        CHECK( api.Net_GetTick() == static_cast<int>( pauseAt ) );
        CHECK( api.Net_GetActionIndex( fire ) == 1 );
        CHECK( api.Net_GetLayoutVersion() == static_cast<int>( replayInput.GetLayoutVersion() ) );
        CHECK( ( api.Net_GetLocalCommand( &buttons, &previousButtons, axes, aim, &viewTick, &tick ) & 1 ) == 0 );
        // Net.Press in the replay world holds its own bit, nothing in the live world's.
        api.Net_SetActionOverride( api.Net_GetActionIndex( fire ), true );
        InputCommand sampled;
        replayInput.SampleCommand( replayDevices, sampled );
        CHECK( sampled.Buttons == ( 1u << 1 ) );
    }
    InputCommand liveSampled;
    liveInput.SampleCommand( liveDevices, liveSampled );
    CHECK( liveSampled.Buttons == 1u );
    ScriptBindings::SetWorld( previousWorld );
#endif

    replayNet.Shutdown();
    live.ClientNet.Shutdown();
    live.ServerNet.Shutdown();
}


TEST_CASE( "World singletons: the editor's Stop (Destroy, then ResetLocalInput) keeps the world's input layout, so a session that outlives Stop still samples the next Play (ME-297)" )
{
    InputContext gameplay;
    gameplay.Name = "Gameplay";
    InputAction fire;
    fire.Name = "Fire";
    fire.Type = InputActionType::Button;
    fire.Networked = true;
    gameplay.Actions = { fire };
    InputActionMap map;
    map.Contexts = { gameplay };
    Input devices;
    devices.GetActions().SetMap( map );

    auto world = std::make_shared<World>();
    world->IsLoading = false;
    SingletonInput& input = world->Singleton<SingletonInput>();
    input.BuildCommandLayout( devices );
    input.SetActionOverride( input.GetActionIndex( "Fire" ), true );
    const uint32_t version = input.GetLayoutVersion();

    // EditorApp::StopGame. Engine::Run rebuilds the layout only without an active session, so a
    // Havana --connect/--server session would sample nothing if Stop lost it.
    world->Destroy();
    world->Singleton<SingletonInput>().ResetLocalInput();
    REQUIRE( world->FindSingleton<SingletonInput>() == &input );
    CHECK( input.GetLayoutVersion() == version );
    CHECK( input.HasCommandLayout() );
    CHECK( input.GetActionIndex( "Fire" ) == 0 );
    // What the script held went with the play session.
    InputCommand command;
    input.SampleCommand( devices, command );
    CHECK( command.Buttons == 0u );
}
