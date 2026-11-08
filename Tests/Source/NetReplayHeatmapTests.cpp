#include "PCH.h"
#include <doctest/doctest.h>
#include "JSON.h"
#include "Net/NetReplay.h"
#include "Net/NetReplayDump.h"
#include "Net/NetReplayHeatmap.h"
#include <sstream>
#include <filesystem>
#include <fstream>
#include <string>
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
    // 110 by its own hand.
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

    // Positions: a sample every 6 ticks (0.1 s), 20 per character.
    CHECK( heatmap.GetTotal( Layer::Positions ) == doctest::Approx( 4.0 ) );
    CHECK( heatmap.ValueAt( Layer::Positions, Vector3( 2.5f, 0.f, 3.5f ) ) == doctest::Approx( 1.0 ) );
    CHECK( heatmap.ValueAt( Layer::Positions, Vector3( 10.5f, 0.f, -4.5f ) ) == doctest::Approx( 1.0 ) );
    CHECK( heatmap.ValueAt( Layer::Positions, Vector3( -1.5f, 0.f, 0.5f ) ) == doctest::Approx( 2.0 ) );
    CHECK( heatmap.GetTotal( Layer::Positions, "hero:Striker" ) == doctest::Approx( 2.0 ) );
    CHECK( heatmap.GetTotal( Layer::Positions, "team:Blue" ) == doctest::Approx( 1.8 ) );
    // Before the picks, nobody had a team: ticks 0 and 6 aren't on Red.
    CHECK( heatmap.GetTotal( Layer::Positions, "team:Red" ) == doctest::Approx( 1.8 ) );

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
    CHECK( heatmap.GetSources()[0].Samples == 40 );
    CHECK( heatmap.GetSources()[0].Seconds == doctest::Approx( 2.0 ) );
    const std::string text = heatmap.Describe();
    CHECK( text.find( "1 replay(s) of Assets/Scenes/Arena.lvl" ) != std::string::npos );
    CHECK( text.find( "1 kill(s) without a known killer and 1 by the victim itself" ) != std::string::npos );
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
    CHECK( heatmap.GetTotal( Layer::Positions ) == doctest::Approx( 8.0 ) );
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
    CHECK( written.GetTotal( Layer::Positions ) == doctest::Approx( 8.0 ) );
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
