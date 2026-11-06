#include "PCH.h"
#include <doctest/doctest.h>
#include "Components/Networking/NetIdentity.h"
#include "Components/Transform.h"
#include "Net/NetReplay.h"
#include "Net/NetReplayPlayback.h"
#include "Net/NetReplication.h"
#include "NetHarness.h"
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
