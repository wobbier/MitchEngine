#include "PCH.h"
#include <doctest/doctest.h>
#include "JSON.h"
#include "Components/Networking/NetHitbox.h"
#include "Components/Networking/NetIdentity.h"
#include "Components/Networking/SingletonInput.h"
#include "Components/Physics/CharacterController.h"
#include "Components/Physics/Colliders.h"
#include "Core/UpdateContext.h"
#include "Cores/PhysicsCore.h"
#include "Debug/DebugDraw.h"
#include "Engine/HeatmapOverlay.h"
#include "Components/Transform.h"
#include "Cores/NetCore.h"
#include "Engine/World.h"
#include "Net/NetTransport.h"
#include "Net/NetReplay.h"
#include "Net/NetReplayDump.h"
#include "Net/NetReplayHeatmap.h"
#include "Net/NetReplayTimeline.h"
#include <chrono>
#include <cmath>
#include <sstream>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

// ME-215: heatmaps of where players were, died and killed, from one pass over a replay (or many).
namespace
{
    constexpr uint32_t kStriker = 65537;
    constexpr uint32_t kBruiser = 65539;

    NetReplayHeader HeatmapHeader( uint64_t InScene = 0xA11CE )
    {
        NetReplayHeader header;
        header.Product = "HeatmapTest 1.0";
        header.Map = "Assets/Scenes/Arena.lvl";
        header.TickRate = 60;
        header.KeyframeInterval = 50;
        header.SceneHash = InScene;
        return header;
    }

    void Mover( NetReplayTick& InTick, uint32_t InNetId, const Vector3& InPosition )
    {
        InTick.State.Entities[InNetId].SetPose( InPosition, Quaternion() );
        NetReplayMover& mover = InTick.Movers.emplace_back();
        mover.NetId = InNetId;
        mover.State.Position = InPosition;
        mover.State.Grounded = true;
    }

    // 120 ticks at 60 Hz in 3 chunks. Striker stands at (2.5, 1, 3.5) until tick 60, then at (10.5, 1, -4.5);
    // Bruiser at (-1.5, 1, 0.5) throughout. Picks at 10 put them on teams Red and Blue. Bruiser's shot hits
    // Striker at 40 and Striker dies at 45 (the payload's "at": (2.2, 1, 3.9)), no killer named: the shot's.
    // Striker kills Bruiser at 90 ("killerNetId"). Bruiser dies at 100 with nobody to blame (a fall) and at
    // 110 by its own hand. Striker respawns at 60, Bruiser at 95 and 105: the dead aren't sampled in between.
    std::vector<NetReplayTick> HeatmapMatch()
    {
        std::vector<NetReplayTick> ticks;
        for( uint32_t t = 0; t < 120; ++t )
        {
            NetReplayTick tick;
            tick.Tick = t;
            tick.State.Tick = t;
            Mover( tick, kStriker, t < 60 ? Vector3( 2.5f, 1.f, 3.5f ) : Vector3( 10.5f, 1.f, -4.5f ) );
            Mover( tick, kBruiser, Vector3( -1.5f, 1.f, 0.5f ) );
            if( t % 50 == 0 )
            {
                tick.Entities = { { kStriker, "Striker", "Assets/Prefabs/Heroes/Striker.prefab", 1 }, { kBruiser, "Bruiser", "Assets/Prefabs/Heroes/Bruiser.prefab", 2 } };
            }
            if( t == 10 )
            {
                tick.Events.push_back( { "pick", "{\"player\":\"Alpha\",\"team\":\"Red\"}", kStriker } );
                tick.Events.push_back( { "pick", "{\"player\":\"Bravo\",\"team\":\"Blue\"}", kBruiser } );
            }
            if( t == 40 )
            {
                NetReplayShot& shot = tick.Shots.emplace_back();
                shot.Client = 2;
                shot.Shooter = kBruiser;
                shot.Origin = Vector3( -1.5f, 2.f, 0.5f );
                shot.Direction = Vector3( 1.f, 0.f, 0.f );
                shot.Hit = true;
                shot.Target = kStriker;
            }
            if( t == 45 )
            {
                tick.Events.push_back( { "kill", "{\"killer\":\"Bravo\",\"victim\":\"Alpha\",\"at\":[2.2,1,3.9]}", kStriker } );
            }
            if( t == 60 )
            {
                tick.Events.push_back( { "respawn", "{}", kStriker } );
            }
            if( t == 95 || t == 105 )
            {
                tick.Events.push_back( { "respawn", "{}", kBruiser } );
            }
            if( t == 90 )
            {
                tick.Events.push_back( { "kill", "{\"killer\":\"Alpha\",\"victim\":\"Bravo\",\"killerNetId\":65537}", kBruiser } );
            }
            if( t == 100 )
            {
                tick.Events.push_back( { "kill", "{\"victim\":\"Bravo\"}", kBruiser } );
            }
            if( t == 110 )
            {
                tick.Events.push_back( { "kill", "{\"killerNetId\":65539}", kBruiser } );
            }
            ticks.push_back( std::move( tick ) );
        }
        return ticks;
    }

    NetReplayMemorySource HeatmapSource( uint64_t InScene = 0xA11CE )
    {
        NetReplayMemorySource source( HeatmapHeader( InScene ) );
        const std::vector<NetReplayTick> ticks = HeatmapMatch();
        for( size_t first = 0; first < ticks.size(); first += 50 )
        {
            std::vector<NetReplayTick> chunk( ticks.begin() + first, ticks.begin() + std::min<size_t>( first + 50, ticks.size() ) );
            REQUIRE( source.AddChunk( NetReplayChunk::Encode( chunk, NetReplayCodec::Deflate ) ) );
        }
        return source;
    }

    using Layer = NetReplayHeatmap::Layer;
}


TEST_CASE( "Replay heatmap: positions, deaths and kills from one pass land in the known cells, per hero and team (ME-215)" )
{
    NetReplayMemorySource source = HeatmapSource();
    NetReplayReader reader( source );
    NetReplayHeatmap heatmap;
    std::string error;
    REQUIRE_MESSAGE( heatmap.AddReplay( reader, "synthetic.mreplay", &error ), error );

    // Positions: a sample every 6 ticks (0.1 s), none while dead: Striker's at 0..42 and 60..114, Bruiser's at
    // 0..84, 96 and 108.
    CHECK( heatmap.GetTotal( Layer::Positions ) == doctest::Approx( 3.5 ) );
    CHECK( heatmap.ValueAt( Layer::Positions, Vector3( 2.5f, 0.f, 3.5f ) ) == doctest::Approx( 0.8 ) );
    CHECK( heatmap.ValueAt( Layer::Positions, Vector3( 10.5f, 0.f, -4.5f ) ) == doctest::Approx( 1.0 ) );
    CHECK( heatmap.ValueAt( Layer::Positions, Vector3( -1.5f, 0.f, 0.5f ) ) == doctest::Approx( 1.7 ) );
    CHECK( heatmap.GetTotal( Layer::Positions, "hero:Striker" ) == doctest::Approx( 1.8 ) );
    // Before the picks, nobody had a team: ticks 0 and 6 aren't on a team.
    CHECK( heatmap.GetTotal( Layer::Positions, "team:Blue" ) == doctest::Approx( 1.5 ) );
    CHECK( heatmap.GetTotal( Layer::Positions, "team:Red" ) == doctest::Approx( 1.6 ) );

    // Deaths: Striker's at the payload's point, Bruiser's three where it stood.
    CHECK( heatmap.GetTotal( Layer::Deaths ) == doctest::Approx( 4.0 ) );
    CHECK( heatmap.ValueAt( Layer::Deaths, Vector3( 2.2f, 0.f, 3.9f ) ) == doctest::Approx( 1.0 ) );
    CHECK( heatmap.ValueAt( Layer::Deaths, Vector3( -1.5f, 0.f, 0.5f ) ) == doctest::Approx( 3.0 ) );
    CHECK( heatmap.GetTotal( Layer::Deaths, "hero:Bruiser" ) == doctest::Approx( 3.0 ) );
    CHECK( heatmap.GetTotal( Layer::Deaths, "team:Red" ) == doctest::Approx( 1.0 ) );

    // Kills: Bruiser's (from the shot that hit Striker) where Bruiser stood, Striker's (named) where it stood.
    CHECK( heatmap.GetTotal( Layer::Kills ) == doctest::Approx( 2.0 ) );
    CHECK( heatmap.ValueAt( Layer::Kills, Vector3( -1.5f, 0.f, 0.5f ) ) == doctest::Approx( 1.0 ) );
    CHECK( heatmap.ValueAt( Layer::Kills, Vector3( 10.5f, 0.f, -4.5f ) ) == doctest::Approx( 1.0 ) );
    CHECK( heatmap.GetTotal( Layer::Kills, "hero:Bruiser" ) == doctest::Approx( 1.0 ) );
    CHECK( heatmap.GetTotal( Layer::Kills, "team:Red" ) == doctest::Approx( 1.0 ) );
    CHECK( heatmap.GetUnattributedKills() == 1 );
    CHECK( heatmap.GetSuicides() == 1 );

    const std::vector<std::string> groups = heatmap.GetGroups();
    CHECK( groups == std::vector<std::string>{ "all", "hero:Bruiser", "hero:Striker", "team:Blue", "team:Red" } );

    // The grid over every layer's cells: x -2..10, z -5..3.
    const NetReplayHeatmap::Grid grid = heatmap.GetGrid( Layer::Deaths );
    CHECK( grid.MinX == -2 );
    CHECK( grid.MinZ == -5 );
    CHECK( grid.Width == 13 );
    CHECK( grid.Height == 9 );
    CHECK( grid.Max == doctest::Approx( 3.f ) );
    CHECK( grid.At( 0, 5 ) == doctest::Approx( 3.f ) );     // (-2, 0)
    CHECK( grid.At( 4, 8 ) == doctest::Approx( 1.f ) );     // (2, 3)
    // The floor: the lowest sample in the cell.
    CHECK( heatmap.CellCentre( 2, 3 ).y == doctest::Approx( 1.f ) );
    CHECK( heatmap.CellCentre( 2, 3 ).x == doctest::Approx( 2.5f ) );

    REQUIRE( heatmap.GetSources().size() == 1 );
    CHECK( heatmap.GetSources()[0].Kills == 4 );
    CHECK( heatmap.GetSources()[0].Samples == 35 );
    CHECK( heatmap.GetSources()[0].Seconds == doctest::Approx( 2.0 ) );
    const std::string text = heatmap.Describe();
    CHECK( text.find( "1 replay(s) of Assets/Scenes/Arena.lvl" ) != std::string::npos );
    CHECK( text.find( "1 kill(s) without a known killer and 1 by the victim itself" ) != std::string::npos );
}


TEST_CASE( "Replay heatmap: a dead killer's kill is where it stood alive, and a hit its shooter died after, or by another player than the payload names, isn't the kill (ME-215)" )
{
    // Striker (Alpha), Bruiser (Bravo) and Railgunner (Romeo). Romeo hits Striker at 10, Bruiser kills
    // Romeo at 20 (named), and Romeo's body is parked under the map from 30 (HeroShooter's limbo).
    // Striker dies at 40 by "Bravo", with no hit of Bravo's: Romeo's hit landed before Romeo died, so it
    // isn't the kill. Romeo's rocket (fired alive, its sweep at 50) kills Bruiser at 50: Romeo's, where
    // Romeo last stood alive. Striker hits Bruiser at 60 and Bruiser dies at 65 by "Charlie": neither
    // Alpha's hit nor Romeo's is the kill.
    constexpr uint32_t kRailgunner = 65541;
    const Vector3 romeoAt( 6.5f, 1.f, 6.5f );
    const Vector3 limbo( -4.5f, -58.5f, 4.5f );
    std::vector<NetReplayTick> ticks;
    for( uint32_t t = 0; t < 80; ++t )
    {
        NetReplayTick tick;
        tick.Tick = t;
        tick.State.Tick = t;
        Mover( tick, kStriker, Vector3( 2.5f, 1.f, 3.5f ) );
        Mover( tick, kBruiser, Vector3( -1.5f, 1.f, 0.5f ) );
        Mover( tick, kRailgunner, t < 30 ? romeoAt : limbo );
        if( t % 50 == 0 )
        {
            tick.Entities = { { kStriker, "Striker", "Assets/Prefabs/Heroes/Striker.prefab", 1 }, { kBruiser, "Bruiser", "Assets/Prefabs/Heroes/Bruiser.prefab", 2 },
                { kRailgunner, "Railgunner", "Assets/Prefabs/Heroes/Railgunner.prefab", 3 } };
        }
        auto hit = [&]( uint32_t InShooter, uint32_t InInstigator, uint32_t InTarget, const Vector3& InOrigin ) {
            NetReplayShot& shot = tick.Shots.emplace_back();
            shot.Shooter = InShooter;
            shot.Instigator = InInstigator;
            shot.Origin = InOrigin;
            shot.Direction = Vector3( 1.f, 0.f, 0.f );
            shot.Hit = true;
            shot.Target = InTarget;
        };
        if( t == 1 )
        {
            tick.Events.push_back( { "spawn", "{\"player\":\"Alpha\"}", kStriker } );
            tick.Events.push_back( { "spawn", "{\"player\":\"Bravo\"}", kBruiser } );
            tick.Events.push_back( { "spawn", "{\"player\":\"Romeo\"}", kRailgunner } );
        }
        if( t == 10 )
        {
            hit( kRailgunner, 0, kStriker, romeoAt );
        }
        if( t == 20 )
        {
            tick.Events.push_back( { "kill", "{\"killer\":\"Bravo\",\"killerNetId\":65539}", kRailgunner } );
        }
        if( t == 40 )
        {
            tick.Events.push_back( { "kill", "{\"killer\":\"Bravo\"}", kStriker } );
        }
        if( t == 50 )
        {
            hit( 0, kRailgunner, kBruiser, Vector3( -1.f, 1.f, 0.5f ) );
            tick.Events.push_back( { "kill", "{\"killer\":\"Romeo\"}", kBruiser } );
        }
        if( t == 55 )
        {
            tick.Events.push_back( { "respawn", "{\"player\":\"Alpha\"}", kStriker } );
            tick.Events.push_back( { "respawn", "{\"player\":\"Bravo\"}", kBruiser } );
        }
        if( t == 60 )
        {
            hit( kStriker, 0, kBruiser, Vector3( 2.5f, 2.f, 3.5f ) );
        }
        if( t == 65 )
        {
            tick.Events.push_back( { "kill", "{\"killer\":\"Charlie\"}", kBruiser } );
        }
        ticks.push_back( std::move( tick ) );
    }
    NetReplayMemorySource source( HeatmapHeader() );
    for( size_t first = 0; first < ticks.size(); first += 50 )
    {
        std::vector<NetReplayTick> chunk( ticks.begin() + first, ticks.begin() + std::min<size_t>( first + 50, ticks.size() ) );
        REQUIRE( source.AddChunk( NetReplayChunk::Encode( chunk, NetReplayCodec::Deflate ) ) );
    }
    NetReplayReader reader( source );
    NetReplayHeatmap heatmap;
    std::string error;
    REQUIRE_MESSAGE( heatmap.AddReplay( reader, "dead-killer.mreplay", &error ), error );

    CHECK( heatmap.GetTotal( Layer::Deaths ) == doctest::Approx( 4.0 ) );
    // Bravo's named kill of Romeo where Bravo stands; Romeo's rocket kill where Romeo stood alive.
    CHECK( heatmap.GetTotal( Layer::Kills ) == doctest::Approx( 2.0 ) );
    CHECK( heatmap.ValueAt( Layer::Kills, Vector3( -1.5f, 0.f, 0.5f ) ) == doctest::Approx( 1.0 ) );
    CHECK( heatmap.ValueAt( Layer::Kills, romeoAt ) == doctest::Approx( 1.0 ) );
    CHECK( heatmap.GetTotal( Layer::Kills, "hero:Railgunner" ) == doctest::Approx( 1.0 ) );
    CHECK( heatmap.ValueAt( Layer::Kills, limbo ) == doctest::Approx( 0.0 ) );
    CHECK( heatmap.GetTotal( Layer::Kills, "hero:Striker" ) == doctest::Approx( 0.0 ) );
    // Striker's death at 40 and Bruiser's at 65 have no killer the replay can tell.
    CHECK( heatmap.GetUnattributedKills() == 2 );
    // Nothing went in under the map: the parked body isn't a position, a death or a kill.
    int x = 0;
    int z = 0;
    heatmap.CellOf( limbo, x, z );
    CHECK( std::isnan( heatmap.FloorAt( x, z ) ) );
    heatmap.CellOf( romeoAt, x, z );
    CHECK( heatmap.FloorAt( x, z ) == doctest::Approx( 1.f ) );
}


TEST_CASE( "Replay heatmap: many replays add up, another scene is refused, and the JSON and PNG keep it (ME-215)" )
{
    NetReplayHeatmap heatmap( NetReplayHeatmap::Settings{ 1.f, 6, "kill", 10.0 } );
    std::string error;
    for( int i = 0; i < 2; ++i )
    {
        NetReplayMemorySource source = HeatmapSource();
        NetReplayReader reader( source );
        REQUIRE_MESSAGE( heatmap.AddReplay( reader, "match" + std::to_string( i ) + ".mreplay", &error ), error );
    }
    CHECK( heatmap.GetTotal( Layer::Positions ) == doctest::Approx( 7.0 ) );
    CHECK( heatmap.GetTotal( Layer::Deaths ) == doctest::Approx( 8.0 ) );
    CHECK( heatmap.GetTotal( Layer::Kills ) == doctest::Approx( 4.0 ) );

    // Another map would mix cells that aren't the same places: refused, nothing added.
    {
        NetReplayMemorySource other = HeatmapSource( 0xB0B );
        NetReplayReader reader( other );
        CHECK_FALSE( heatmap.AddReplay( reader, "other.mreplay", &error ) );
        CHECK( error.find( "another scene" ) != std::string::npos );
        CHECK( heatmap.GetSources().size() == 2 );
        CHECK( heatmap.GetTotal( Layer::Deaths ) == doctest::Approx( 8.0 ) );
    }

    // JSON round trip: the same cells, totals, floor and sources.
    const json saved = heatmap.ToJson();
    CHECK( saved["format"] == "mheatmap" );
    CHECK( saved["layers"]["deaths"]["groups"]["all"]["total"].get<double>() == doctest::Approx( 8.0 ) );
    NetReplayHeatmap loaded;
    REQUIRE_MESSAGE( loaded.FromJson( saved, &error ), error );
    CHECK( loaded.ToJson() == saved );
    CHECK( loaded.ValueAt( Layer::Deaths, Vector3( -1.5f, 0.f, 0.5f ) ) == doctest::Approx( 6.0 ) );
    CHECK( loaded.GetSources().size() == 2 );
    CHECK( loaded.GetSceneHash() == 0xA11CEull );
    CHECK_FALSE( loaded.FromJson( json::object(), &error ) );
    CHECK( loaded.GetTotal( Layer::Deaths ) == doctest::Approx( 8.0 ) );   // a failed load leaves it

    // PNGs: one per layer and the overview, sized by the grid (13 x 9 cells, 16 px a cell).
    const std::filesystem::path folder = std::filesystem::temp_directory_path() / "MitchEngineHeatmapTest";
    std::filesystem::remove_all( folder );
    REQUIRE_MESSAGE( heatmap.WritePng( ( folder / "deaths.png" ).string(), Layer::Deaths, "all", &error ), error );
    REQUIRE_MESSAGE( heatmap.WriteOverviewPng( ( folder / "overview.png" ).string(), "all", &error ), error );
    REQUIRE_MESSAGE( heatmap.Save( ( folder / "heatmap.json" ).string(), &error ), error );
    std::ifstream png( folder / "deaths.png", std::ios::binary );
    std::vector<uint8_t> bytes( ( std::istreambuf_iterator<char>( png ) ), std::istreambuf_iterator<char>() );
    REQUIRE( bytes.size() > 33 );
    CHECK( bytes[0] == 0x89 );
    CHECK( bytes[1] == 'P' );
    CHECK( std::string( bytes.begin() + 12, bytes.begin() + 16 ) == "IHDR" );
    const uint32_t width = static_cast<uint32_t>( bytes[16] ) << 24 | bytes[17] << 16 | bytes[18] << 8 | bytes[19];
    const uint32_t height = static_cast<uint32_t>( bytes[20] ) << 24 | bytes[21] << 16 | bytes[22] << 8 | bytes[23];
    CHECK( width == 13 * 16 );
    CHECK( height == 9 * 16 );
    NetReplayHeatmap reread;
    REQUIRE_MESSAGE( reread.Load( ( folder / "heatmap.json" ).string(), &error ), error );
    CHECK( reread.ToJson() == saved );
    std::filesystem::remove_all( folder );
}


TEST_CASE( "Replay heatmap: a sample far off the map is left out, a file's stray cells stay inside the grid, and one too big to draw says so (ME-215)" )
{
    // A character flung 5 km away doesn't stretch the grid over kilometres of nothing.
    NetReplayHeatmap heatmap;
    heatmap.AddPoint( Layer::Positions, Vector3( 0.5f, 1.f, 0.5f ), "Striker", "" );
    heatmap.AddPoint( Layer::Positions, Vector3( 5000.f, 1.f, 0.5f ), "Striker", "" );
    heatmap.AddPoint( Layer::Deaths, Vector3( 1.f, 1.f, -1e30f ), "Striker", "" );
    CHECK( heatmap.GetOutOfRange() == 2 );
    NetReplayHeatmap::Grid grid = heatmap.GetGrid( Layer::Positions );
    CHECK( grid.Width == 1 );
    CHECK( grid.Height == 1 );
    CHECK( heatmap.Describe().find( "2 sample(s) more than 1000 m from the origin left out" ) != std::string::npos );

    // A file whose hero group has a cell "all" lacks (edited by hand): the grid spans it, nothing is
    // written outside it.
    json edited = heatmap.ToJson();
    edited["layers"]["positions"]["groups"]["hero:Striker"]["cells"].push_back( { 40, -30, 2.0 } );
    NetReplayHeatmap loaded;
    std::string error;
    REQUIRE_MESSAGE( loaded.FromJson( edited, &error ), error );
    CHECK( loaded.GetOutOfRange() == 2 );
    grid = loaded.GetGrid( Layer::Positions, "hero:Striker" );
    CHECK( grid.MinX == 0 );
    CHECK( grid.MinZ == -30 );
    CHECK( grid.Width == 41 );
    CHECK( grid.Height == 31 );
    CHECK( grid.Total == doctest::Approx( 3.0 ) );
    CHECK( grid.At( 40, 0 ) == doctest::Approx( 2.f ) );
    CHECK( loaded.GetGrid( Layer::Positions ).Total == doctest::Approx( 1.0 ) );

    // Cells a file puts 100 km apart: no grid (no 10^10-cell allocation), and the PNG says why.
    json far = heatmap.ToJson();
    far["layers"]["deaths"]["groups"]["all"]["cells"] = json::array( { json::array( { -50000, -50000, 1.0 } ), json::array( { 50000, 50000, 1.0 } ) } );
    REQUIRE_MESSAGE( loaded.FromJson( far, &error ), error );
    grid = loaded.GetGrid( Layer::Deaths );
    CHECK( grid.Width == 0 );
    CHECK( grid.Values.empty() );
    const std::filesystem::path folder = std::filesystem::temp_directory_path() / "MitchEngineHeatmapFarTest";
    CHECK_FALSE( loaded.WritePng( ( folder / "far.png" ).string(), Layer::Deaths, "all", &error ) );
    CHECK( error.find( "cells, more than" ) != std::string::npos );
    CHECK_FALSE( loaded.WriteOverviewPng( ( folder / "far.png" ).string(), "all", &error ) );
    CHECK( loaded.Describe().find( "100001x100001 cells" ) != std::string::npos );
    CHECK_FALSE( std::filesystem::exists( folder / "far.png" ) );
}


TEST_CASE( "ReplayDump --heatmap: replays and folders of them into one heatmap's JSON and PNGs, a bad one skipped (ME-215)" )
{
    namespace fs = std::filesystem;
    const fs::path folder = fs::temp_directory_path() / "MitchEngineHeatmapDump";
    fs::remove_all( folder );
    fs::create_directories( folder / "more" );
    auto record = []( const fs::path& InPath ) {
        NetReplaySettings settings;
        settings.KeyframeInterval = 50;
        NetReplayRecorder recorder( HeatmapHeader(), settings );
        REQUIRE( recorder.OpenFile( InPath.string() ) );
        for( NetReplayTick& tick : HeatmapMatch() )
        {
            recorder.Record( std::move( tick ) );
        }
        recorder.Close();
        REQUIRE_FALSE( recorder.HasFailed() );
    };
    record( folder / "a.mreplay" );
    record( folder / "more" / "b.mreplay" );
    {
        std::ofstream torn( folder / "more" / "c.mreplay", std::ios::binary );
        torn << "not a replay";
    }

    NetReplayDump::Options options;
    std::string error;
    // Several replays only with --heatmap.
    CHECK_FALSE( NetReplayDump::ParseArgs( { "Player", "--replay-dump", ( folder / "a.mreplay" ).string(), ( folder / "more" ).string() }, options, error ) );
    CHECK( error.find( "only --heatmap" ) != std::string::npos );
    CHECK_FALSE( NetReplayDump::ParseArgs( { "Player", "--replay-dump", "x.mreplay", "--heatmap" }, options, error ) );
    CHECK_FALSE( NetReplayDump::ParseArgs( { "Player", "--replay-dump", "x.mreplay", "--heatmap", "out", "--heatmap-cell", "0" }, options, error ) );

    const std::string out = ( folder / "Heatmaps" / "match.json" ).generic_string();
    REQUIRE_MESSAGE( NetReplayDump::ParseArgs( { "Player", "--replay-dump", ( folder / "a.mreplay" ).string(), ( folder / "more" ).string(), "--heatmap", out,
                                                 "--heatmap-cell", "2", "--heatmap-groups" },
                         options, error ),
        error );
    CHECK( options.MorePaths.size() == 1 );
    CHECK( options.HeatmapCell == doctest::Approx( 2.f ) );
    CHECK( NetReplayDump::ExpandPaths( options ).size() == 3 );
    std::ostringstream text;
    REQUIRE_MESSAGE( NetReplayDump::WriteHeatmap( options, text, error ), error );
    const std::string printed = text.str();
    CHECK( printed.find( "skipped " + ( folder / "more" / "c.mreplay" ).generic_string() ) != std::string::npos );
    CHECK( printed.find( "heatmap: 2 replay(s)" ) != std::string::npos );
    for( const char* file : { "match.json", "match.png", "match.positions.png", "match.deaths.png", "match.kills.png", "match.hero-Striker.png", "match.team-Blue.png" } )
    {
        CHECK_MESSAGE( fs::exists( folder / "Heatmaps" / file ), file );
        CHECK( printed.find( std::string( "wrote " ) + ( folder / "Heatmaps" / file ).generic_string() ) != std::string::npos );
    }
    NetReplayHeatmap written;
    REQUIRE_MESSAGE( written.Load( out, &error ), error );
    CHECK( written.GetSettings().CellSize == doctest::Approx( 2.f ) );
    CHECK( written.GetTotal( Layer::Positions ) == doctest::Approx( 7.0 ) );
    CHECK( written.GetTotal( Layer::Deaths ) == doctest::Approx( 8.0 ) );
    CHECK( written.GetTotal( Layer::Kills, "hero:Striker" ) == doctest::Approx( 2.0 ) );
    // 2 m cells: Bruiser's (-1.5, 0.5) and Striker's death at (2.2, 3.9) are cells (-1, 0) and (1, 1).
    CHECK( written.ValueAt( Layer::Deaths, Vector3( -1.5f, 0.f, 0.5f ) ) == doctest::Approx( 6.0 ) );
    CHECK( written.ValueAt( Layer::Deaths, Vector3( 2.2f, 0.f, 3.9f ) ) == doctest::Approx( 2.0 ) );

    // None read: an error, nothing written.
    REQUIRE( NetReplayDump::ParseArgs( { "Player", "--replay-dump", ( folder / "more" / "c.mreplay" ).string(), "--heatmap", ( folder / "none" ).string() }, options, error ) );
    std::ostringstream none;
    CHECK_FALSE( NetReplayDump::WriteHeatmap( options, none, error ) );
    CHECK( error.find( "none of the 1" ) != std::string::npos );
    CHECK_FALSE( fs::exists( folder / "none" ) );
    fs::remove_all( folder );
}


TEST_CASE( "Offline recording: a session with no network records like a server, the local command as client 1's and present shots with their instigator; a heatmap reads it (ME-215)" )
{
    namespace fs = std::filesystem;
    NetCore net;    // before the world: it must outlive it
    std::shared_ptr<World> world = std::make_shared<World>();
    world->IsLoading = false;
    world->AddCore( net );
    auto place = [&]( uint64_t InGuid, const char* InName, const Vector3& InAt ) {
        EntityHandle entity = world->CreateEntityWithGUID( InGuid );
        entity->SetName( InName );
        entity->AddComponent<Transform>().SetPosition( InAt );
        entity->AddComponent<NetIdentity>();
        return entity;
    };
    // The player's pawn (a character), a target with a hitbox, and the player's rocket.
    EntityHandle pawn = place( 0x9A11, "Pawn", Vector3( 0.f, 1.f, -10.f ) );
    CharacterController& mover = pawn->AddComponent<CharacterController>();
    EntityHandle target = place( 0x7A26, "Target", Vector3( 0.5f, 1.f, 10.5f ) );
    NetHitbox& hitbox = target->AddComponent<NetHitbox>();
    hitbox.Radius = 0.4f;
    hitbox.Height = 1.8f;
    EntityHandle rocket = place( 0x60C7, "Rocket", Vector3( 0.f, 1.f, 5.f ) );
    world->Simulate();
    world->Start();

    const uint64_t scene = net.MakeVersion().Scene;     // the placed networked entities
    const fs::path path = fs::temp_directory_path() / "MitchEngineOffline.mreplay";
    fs::remove( path );
    NetReplayHeader header;
    header.Map = "Offline";
    NetReplaySettings settings;
    settings.KeyframeInterval = 40;
    REQUIRE( net.StartOfflineRecording( path.string(), header, 60, settings ) );
    // Nothing about the session changes: gameplay stays offline.
    CHECK( net.IsRecording() );
    CHECK( net.IsRecordingOffline() );
    CHECK_FALSE( net.IsActive() );
    CHECK_FALSE( net.IsServer() );
    CHECK_FALSE( net.IsClient() );
    CHECK_FALSE( net.StartRecording( path.string() + ".server" ) );    // a server's (there's none)
    CHECK_FALSE( net.StartOfflineRecording( path.string() + ".again", header, 60 ) );
    CHECK( net.GetNetId( *target.Get() ) != 0u );     // placed: static ids
    CHECK( net.FindEntity( net.GetNetId( *target.Get() ) ) == target );

    SingletonInput& input = world->Singleton<SingletonInput>();
    std::map<uint32_t, InputCommand> commands;
    std::map<uint32_t, CharacterController::NetState> movers;
    EntityHandle spawned;
    uint32_t shotTick = 0;
    uint32_t sweepTick = 0;
    LagCompensatedHit shotHit;
    double now = 0.0;
    for( uint32_t step = 0; step < 100; ++step )
    {
        InputCommand command;
        command.Buttons = step % 10 == 0 ? 1u : 0u;
        command.Axes = { 0.f, 1.f, 0.f, 0.f };
        input.RecordLocalCommand( command );
        net.BeginTick( 0 );
        const uint32_t tick = net.GetTick();
        commands[tick] = input.GetLocalCommand();
        CharacterController::NetState state;
        state.Position = Vector3( 0.25f * static_cast<float>( step ), 1.f, -10.f );
        state.Grounded = true;
        mover.SetNetState( state );
        movers[tick] = mover.GetNetState();
        if( step == 30 )
        {
            // The player's trigger pull (Net.LagCompensatedRaycast offline): judged as things are.
            shotTick = tick;
            shotHit = net.PresentRaycast( Vector3( 0.5f, 1.f, -10.f ), Vector3( 0.f, 0.f, 1.f ), 100.f, pawn );
            CHECK( shotHit.Hit );
        }
        if( step == 50 )
        {
            // The rocket's sweep, fired by the pawn.
            sweepTick = tick;
            CHECK( net.PresentRaycast( Vector3( 0.5f, 1.f, 5.f ), Vector3( 0.f, 0.f, 1.f ), 10.f, rocket, pawn ).Hit );
            net.RecordReplayEvent( "kill", "{\"killer\":\"Player\",\"victim\":\"Target\"}", target );
        }
        if( step == 60 )
        {
            // Spawned mid-recording: its NetId comes with the tick's capture, and the event names it.
            spawned = world->CreateEntity();
            spawned->SetName( "Pickup" );
            spawned->AddComponent<Transform>().SetPosition( Vector3( 3.f, 0.f, 3.f ) );
            spawned->AddComponent<NetIdentity>();
            net.RecordReplayEvent( "spawn", "{}", spawned );
        }
        world->Simulate();
        now += 1.0 / 60.0;
        net.EndTick( now );
    }
    // A shot outside a tick isn't the recording's (an editor tool's, a frame's).
    net.PresentRaycast( Vector3( 0.f, 1.f, -10.f ), Vector3( 0.f, 0.f, 1.f ), 100.f, pawn );
    CHECK( net.GetNetId( *spawned.Get() ) != 0u );
    net.StopRecording();
    CHECK_FALSE( net.IsRecording() );
    CHECK_FALSE( net.IsRecordingOffline() );
    REQUIRE( net.GetRecorder() );
    CHECK_FALSE( net.GetRecorder()->HasFailed() );
    CHECK( net.GetRecorder()->GetTicksRecorded() == 100 );

    NetReplayFileSource source;
    REQUIRE_MESSAGE( source.Open( path.string() ), source.GetError() );
    CHECK( source.GetHeader().TickRate == 60 );
    CHECK( source.GetHeader().Map == "Offline" );
    CHECK( source.GetHeader().SceneHash == scene );
    NetReplayReader reader( source );
    CHECK( reader.GetTickCount() == 100 );
    const uint32_t pawnId = net.GetNetId( *pawn.Get() );
    const uint32_t targetId = net.GetNetId( *target.Get() );
    const uint32_t rocketId = net.GetNetId( *rocket.Get() );
    size_t commandsSeen = 0;
    size_t moversSeen = 0;
    bool joined = false;
    bool killed = false;
    bool spawnNamed = false;
    std::vector<NetReplayShot> shots;
    std::map<uint32_t, std::string> names;
    REQUIRE( reader.ForEach( 0, ~0u, [&]( const NetReplayTick& InTick ) {
        for( const NetReplayEntity& entity : InTick.Entities )
        {
            names[entity.NetId] = entity.Name;
        }
        REQUIRE( InTick.Clients.size() == 1 );
        CHECK( InTick.Clients[0].Id == 1 );
        CHECK( InTick.Clients[0].Command.Tick == InTick.Tick );
        CHECK( InTick.Clients[0].Command.Buttons == commands[InTick.Tick].Buttons );
        CHECK( InTick.Clients[0].Command.Axes == commands[InTick.Tick].Axes );
        ++commandsSeen;
        REQUIRE( InTick.Movers.size() == 1 );
        CHECK( InTick.Movers[0].NetId == pawnId );
        CHECK( InTick.Movers[0].State == movers[InTick.Tick] );
        ++moversSeen;
        CHECK( InTick.State.Entities.count( targetId ) == 1 );
        shots.insert( shots.end(), InTick.Shots.begin(), InTick.Shots.end() );
        for( const NetReplayShot& shot : InTick.Shots )
        {
            CHECK( ( InTick.Tick == shotTick || InTick.Tick == sweepTick ) );
        }
        for( const NetReplayEvent& event : InTick.Events )
        {
            joined |= event.Name == "net.joined" && InTick.Tick == reader.GetFirstTick() && event.Payload.find( "\"client\":1" ) != std::string::npos;
            killed |= event.Name == "kill" && InTick.Tick == sweepTick && event.Entity == targetId;
            spawnNamed |= event.Name == "spawn" && event.Entity != 0 && event.Entity == net.GetNetId( *spawned.Get() );
        }
        return true;
    } ) );
    CHECK( commandsSeen == 100 );
    CHECK( moversSeen == 100 );
    CHECK( joined );
    CHECK( killed );
    CHECK( spawnNamed );
    CHECK( names[pawnId] == "Pawn" );
    CHECK( names[net.GetNetId( *spawned.Get() )] == "Pickup" );
    REQUIRE( shots.size() == 2 );
    CHECK( shots[0].Shooter == pawnId );
    CHECK( shots[0].Instigator == 0u );
    CHECK( shots[0].Hit );
    CHECK( shots[0].Target == targetId );
    CHECK( shots[0].Point.z == doctest::Approx( shotHit.Point.z ) );
    REQUIRE( shots[0].Hitboxes.size() == 1 );     // the target's, as it was judged (the pawn has none)
    CHECK( shots[0].Hitboxes[0].NetId == targetId );
    CHECK( shots[1].Shooter == rocketId );
    CHECK( shots[1].Instigator == pawnId );
    CHECK( shots[1].Target == targetId );

    // A heatmap of it: the kill is the pawn's (the rocket's instigator), where the pawn stood.
    NetReplayHeatmap heatmap;
    std::string error;
    REQUIRE_MESSAGE( heatmap.AddReplay( reader, path.string(), &error ), error );
    CHECK( heatmap.GetTotal( Layer::Kills, "hero:Pawn" ) == doctest::Approx( 1.0 ) );
    CHECK( heatmap.ValueAt( Layer::Kills, movers[sweepTick].Position ) == doctest::Approx( 1.0 ) );
    CHECK( heatmap.ValueAt( Layer::Deaths, Vector3( 0.5f, 1.f, 10.5f ) ) == doctest::Approx( 1.0 ) );
    CHECK( heatmap.GetTotal( Layer::Deaths, "hero:Target" ) == doctest::Approx( 1.0 ) );
    CHECK( heatmap.GetTotal( Layer::Positions ) == doctest::Approx( 100.0 / 60.0 ).epsilon( 0.11 ) );

    // A session ends an offline recording: its file is closed, the session's own capture takes over.
    REQUIRE( net.StartOfflineRecording( "", header, 60 ) );
    CHECK( net.IsRecordingOffline() );
    LoopbackNetwork network;
    LoopbackTransport transport( network, 1 );
    REQUIRE( net.Host( transport, 60 ) );
    CHECK_FALSE( net.IsRecording() );
    CHECK( net.IsServer() );
    CHECK( net.GetReplicationServer() != nullptr );
    CHECK( net.FindEntity( targetId ) == target );
    net.Shutdown();
    fs::remove( path );
}


namespace
{
    struct HeatmapStepContext
        : public UpdateContext
    {
        using UpdateContext::FixedDeltaTime;
        using UpdateContext::InterpolationAlpha;
        using UpdateContext::IsFixedStepActive;
    };
}


TEST_CASE( "Heatmap overlay: each cell lies on the ground under it (characters ignored), coloured by its heat; the console picks layer and group (ME-215)" )
{
    PhysicsCore physics;    // the cores before the world
    auto world = std::make_shared<World>();
    world->IsLoading = false;
    world->AddCore( physics );
    auto box = [&]( const Vector3& InPosition, const Vector3& InSize ) {
        EntityHandle entity = world->CreateEntity();
        entity->AddComponent<Transform>().SetPosition( InPosition );
        entity->AddComponent<BoxCollider>().Size = InSize;
    };
    box( Vector3( 0.f, -0.5f, 0.f ), Vector3( 80.f, 1.f, 80.f ) );     // the floor, its top at 0
    box( Vector3( 2.5f, 0.5f, 3.5f ), Vector3( 1.f, 1.f, 1.f ) );      // a crate, its top at 1
    EntityHandle pawn = world->CreateEntity();                         // a character standing in a cell
    pawn->AddComponent<Transform>().SetPosition( Vector3( 10.5f, 0.9f, -4.5f ) );
    pawn->AddComponent<CharacterController>();
    world->Simulate();
    world->Start();
    HeatmapStepContext context;
    context.FixedDeltaTime = 1.f / 60.f;
    context.InterpolationAlpha = 1.f;
    context.IsFixedStepActive = true;
    physics.FixedUpdate( context );
    context.IsFixedStepActive = false;
    physics.Update( context );

    // Capsule centres 0.9 m over their feet: on the crate, on the floor, where the pawn stands, and
    // high over nothing (past the floor's edge).
    NetReplayHeatmap heatmap;
    heatmap.AddPoint( NetReplayHeatmap::Layer::Positions, Vector3( 2.5f, 1.9f, 3.5f ), "Striker", "" , 4.0 );
    heatmap.AddPoint( NetReplayHeatmap::Layer::Positions, Vector3( -1.5f, 0.9f, 0.5f ), "Bruiser", "", 1.0 );
    heatmap.AddPoint( NetReplayHeatmap::Layer::Positions, Vector3( 10.5f, 0.9f, -4.5f ), "Bruiser", "", 1.0 );
    heatmap.AddPoint( NetReplayHeatmap::Layer::Positions, Vector3( 50.5f, 5.f, 0.5f ), "Bruiser", "", 1.0 );
    heatmap.AddPoint( NetReplayHeatmap::Layer::Deaths, Vector3( -1.5f, 0.9f, 0.5f ), "Bruiser", "" );

    HeatmapOverlay overlay;
    overlay.Set( heatmap, "test" );
    overlay.Draw( *world );
    const std::vector<HeatmapOverlay::Cell>& cells = overlay.GetCells();
    REQUIRE( cells.size() == 4 );
    auto cellAt = [&]( float InX, float InZ ) -> const HeatmapOverlay::Cell* {
        for( const HeatmapOverlay::Cell& cell : overlay.GetCells() )
        {
            if( std::abs( cell.Centre.x - InX ) < 0.01f && std::abs( cell.Centre.z - InZ ) < 0.01f )
            {
                return &cell;
            }
        }
        return nullptr;
    };
    const HeatmapOverlay::Cell* crate = cellAt( 2.5f, 3.5f );
    const HeatmapOverlay::Cell* floor = cellAt( -1.5f, 0.5f );
    const HeatmapOverlay::Cell* underPawn = cellAt( 10.5f, -4.5f );
    const HeatmapOverlay::Cell* nothing = cellAt( 50.5f, 0.5f );
    REQUIRE( crate );
    REQUIRE( floor );
    REQUIRE( underPawn );
    REQUIRE( nothing );
    CHECK( crate->Centre.y == doctest::Approx( 1.04f ).epsilon( 0.01 ) );
    CHECK( floor->Centre.y == doctest::Approx( 0.04f ).epsilon( 0.01 ) );
    CHECK( underPawn->Centre.y == doctest::Approx( 0.04f ).epsilon( 0.01 ) );   // the floor, not the pawn's capsule
    CHECK( nothing->Centre.y == doctest::Approx( 4.14f ).epsilon( 0.01 ) );     // the sample less 0.9 m
    // The hottest is the crate's, at the top of the ramp; the others a half of it (the square root of a quarter).
    CHECK( crate->Value == doctest::Approx( 4.f ) );
    CHECK( crate->Colour.w > floor->Colour.w );
    const Vector3 top = NetReplayHeatmap::Ramp( 1.f );
    CHECK( crate->Colour.x == doctest::Approx( top.x ) );
    CHECK( floor->Colour.x == doctest::Approx( NetReplayHeatmap::Ramp( 0.5f ).x ) );

    // The console: a layer, a group, a group it doesn't have, hidden, the report.
    CHECK( overlay.RunCommand( { "layer", "deaths" } ).find( "deaths of all" ) != std::string::npos );
    overlay.Draw( *world );
    REQUIRE( overlay.GetCells().size() == 1 );
    CHECK( overlay.GetCells()[0].Centre.x == doctest::Approx( -1.5f ) );
    CHECK( overlay.RunCommand( { "layer", "positions" } ).find( "positions" ) != std::string::npos );
    CHECK( overlay.RunCommand( { "group", "hero:Bruiser" } ).find( "positions of hero:Bruiser" ) != std::string::npos );
    overlay.Draw( *world );
    CHECK( overlay.GetCells().size() == 3 );
    CHECK( overlay.RunCommand( { "group", "hero:Nobody" } ).find( "one of the heatmap's groups" ) != std::string::npos );
    CHECK( overlay.GetGroup() == "hero:Bruiser" );
    CHECK( overlay.RunCommand( { "groups" } ) == "all hero:Bruiser hero:Striker" );
    const json report = overlay.Report();
    CHECK( report["loaded"] == true );
    CHECK( report["layer"] == "positions" );
    CHECK( report["group"] == "hero:Bruiser" );
    CHECK( report["cellsDrawn"] == 3 );
    CHECK( report["total"].get<double>() == doctest::Approx( 3.0 ) );
    overlay.RunCommand( { "off" } );
    CHECK_FALSE( overlay.IsShown() );
    overlay.RunCommand( { "clear" } );
    CHECK_FALSE( overlay.HasHeatmap() );
    CHECK( overlay.RunCommand( {} ) == "heatmap: none loaded" );
    DebugDraw::Clear();
}


TEST_CASE( "Heatmap overlay: a scene load finds the ground again, another map's heatmap is warned about, and a build is cancelled (ME-215)" )
{
    PhysicsCore physics;    // the cores before the world
    auto world = std::make_shared<World>();
    world->IsLoading = false;
    world->AddCore( physics );
    auto box = [&]( const Vector3& InPosition, const Vector3& InSize ) {
        EntityHandle entity = world->CreateEntity();
        entity->AddComponent<Transform>().SetPosition( InPosition );
        entity->AddComponent<BoxCollider>().Size = InSize;
        return entity;
    };
    HeatmapStepContext context;
    context.FixedDeltaTime = 1.f / 60.f;
    context.InterpolationAlpha = 1.f;
    auto step = [&]() {
        context.IsFixedStepActive = true;
        physics.FixedUpdate( context );
        context.IsFixedStepActive = false;
        physics.Update( context );
    };
    EntityHandle floor = box( Vector3( 0.f, -0.5f, 0.f ), Vector3( 80.f, 1.f, 80.f ) );     // its top at 0
    world->Simulate();
    world->Start();
    step();

    NetReplayHeatmap built;
    built.AddPoint( Layer::Positions, Vector3( 2.5f, 2.9f, 3.5f ), "Striker", "" );
    json arena = built.ToJson();
    arena["map"] = "Assets/Scenes/Arena.lvl";
    NetReplayHeatmap heatmap;
    std::string error;
    REQUIRE_MESSAGE( heatmap.FromJson( arena, &error ), error );

    HeatmapOverlay overlay;
    overlay.OnSceneLoaded( "Assets/Scenes/Arena.lvl" );
    overlay.Set( heatmap, "test" );
    CHECK( overlay.MatchesScene() );
    overlay.Draw( *world );
    REQUIRE( overlay.GetCells().size() == 1 );
    CHECK( overlay.GetCells()[0].Centre.y == doctest::Approx( 0.04f ).epsilon( 0.01 ) );

    // The same World, another scene loaded into it (Engine::LoadSceneInternal reuses it): its ground is
    // 2 m up. Until the load is told, the cells keep the old ground; after, they find the new one.
    floor->MarkForDelete();
    box( Vector3( 0.f, 1.5f, 0.f ), Vector3( 80.f, 1.f, 80.f ) );   // its top at 2
    world->Simulate();
    step();
    overlay.Draw( *world );
    CHECK( overlay.GetCells()[0].Centre.y == doctest::Approx( 0.04f ).epsilon( 0.01 ) );
    overlay.OnSceneLoaded( "Assets/Scenes/Payload.lvl" );
    overlay.Draw( *world );
    REQUIRE( overlay.GetCells().size() == 1 );
    CHECK( overlay.GetCells()[0].Centre.y == doctest::Approx( 2.04f ).epsilon( 0.01 ) );
    CHECK_FALSE( overlay.MatchesScene() );
    CHECK( overlay.Describe().find( "warning: recorded on Assets/Scenes/Arena.lvl, drawn over Assets/Scenes/Payload.lvl" ) != std::string::npos );
    CHECK( overlay.Report()["sceneMatches"] == false );

    // A build from replay files: cancelled, nothing is shown whether or not it finished first; let run, shown.
    namespace fs = std::filesystem;
    const fs::path folder = fs::temp_directory_path() / "MitchEngineHeatmapCancel";
    fs::remove_all( folder );
    fs::create_directories( folder );
    for( const char* name : { "a.mreplay", "b.mreplay" } )
    {
        NetReplaySettings settings;
        settings.KeyframeInterval = 50;
        NetReplayRecorder recorder( HeatmapHeader(), settings );
        REQUIRE( recorder.OpenFile( ( folder / name ).string() ) );
        for( NetReplayTick& tick : HeatmapMatch() )
        {
            recorder.Record( std::move( tick ) );
        }
        recorder.Close();
        REQUIRE_FALSE( recorder.HasFailed() );
    }
    auto settle = [&]() {
        for( int i = 0; i < 2000 && overlay.IsBuilding(); ++i )
        {
            std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );
            overlay.Draw( *world );
        }
        REQUIRE_FALSE( overlay.IsBuilding() );
    };
    overlay.Clear();
    CHECK( overlay.RunCommand( { "cancel" } ) == "no heatmap is building" );
    CHECK( overlay.RunCommand( { "build", folder.string() } ).find( "on the background lane" ) != std::string::npos );
    CHECK( overlay.RunCommand( { "cancel" } ) == "cancelling the heatmap build" );
    settle();
    CHECK_FALSE( overlay.HasHeatmap() );
    CHECK( overlay.RunCommand( { "build", folder.string() } ).find( "on the background lane" ) != std::string::npos );
    overlay.Clear();    // clearing cancels too
    settle();
    CHECK_FALSE( overlay.HasHeatmap() );
    REQUIRE( overlay.Build( { folder.string() } ) );
    settle();
    REQUIRE( overlay.HasHeatmap() );
    CHECK( overlay.GetHeatmap()->GetSources().size() == 2 );
    fs::remove_all( folder );
    DebugDraw::Clear();
}


TEST_CASE( "Replay bookmark: the tick, entity, camera and note round-trip, and its replay is found where it was or beside it (ME-215)" )
{
    namespace fs = std::filesystem;
    const fs::path folder = fs::temp_directory_path() / "MitchEngineBookmarks";
    fs::remove_all( folder );
    fs::create_directories( folder / "moved" );
    { std::ofstream( folder / "match.mreplay" ) << "x"; }

    NetReplayBookmark bookmark;
    bookmark.Replay = ( folder / "match.mreplay" ).generic_string();
    bookmark.Tick = 6372;
    bookmark.Entity = 65541;
    bookmark.Camera = "free";
    bookmark.HasFreeCamera = true;
    bookmark.FreePosition = Vector3( 1.5f, 7.25f, -3.f );
    bookmark.FreeYaw = 123.5f;
    bookmark.FreePitch = -20.f;
    bookmark.Note = "the rocket goes through the crate";
    bookmark.Marker = "kill Solo > Bot Ada [Railgunner#65538]";
    bookmark.Map = "Assets/Scenes/Arena.lvl";
    CHECK( NetReplayBookmark::DefaultPath( bookmark.Replay, 6372 ) == ( folder / "match.t6372.mrbookmark" ).generic_string() );

    const std::string path = NetReplayBookmark::DefaultPath( bookmark.Replay, bookmark.Tick );
    std::string error;
    REQUIRE_MESSAGE( bookmark.Save( path, &error ), error );
    NetReplayBookmark read;
    REQUIRE_MESSAGE( read.Load( path, &error ), error );
    CHECK( read.ToJson() == bookmark.ToJson() );
    CHECK( read.Tick == 6372 );
    CHECK( read.Entity == 65541 );
    CHECK( read.HasFreeCamera );
    CHECK( read.FreePosition.y == doctest::Approx( 7.25f ) );
    CHECK( read.FreeYaw == doctest::Approx( 123.5f ) );
    CHECK( read.Note == bookmark.Note );
    CHECK( read.FindReplay( path ) == bookmark.Replay );

    // Moved with its replay: found beside it.
    fs::rename( folder / "match.mreplay", folder / "moved" / "match.mreplay" );
    fs::rename( path, folder / "moved" / "bug.mrbookmark" );
    NetReplayBookmark moved;
    REQUIRE( moved.Load( ( folder / "moved" / "bug.mrbookmark" ).string(), &error ) );
    CHECK( moved.FindReplay( ( folder / "moved" / "bug.mrbookmark" ).string() ) == ( folder / "moved" / "match.mreplay" ).generic_string() );

    // Not a bookmark, or one with no replay: refused, the old one kept.
    CHECK_FALSE( moved.FromJson( json{ { "format", "mheatmap" } }, &error ) );
    CHECK_FALSE( moved.FromJson( json{ { "format", "mrbookmark" }, { "tick", 3 } }, &error ) );
    CHECK( error.find( "no replay" ) != std::string::npos );
    CHECK( moved.Tick == 6372 );
    fs::remove_all( folder );
}
