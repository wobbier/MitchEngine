#include "PCH.h"
#include <doctest/doctest.h>
#include "JSON.h"
#include "Net/NetReplay.h"
#include "Net/NetReplayDump.h"
#include "Net/NetReplayTimeline.h"
#include <cmath>
#include <sstream>
#include <string>
#include <vector>

// ME-316 phase 3: the viewer's timeline (markers to jump to, each client's link along the match)
// from one pass over a replay, and ReplayDump's --markers.
namespace
{
    NetReplayHeader TimelineHeader()
    {
        NetReplayHeader header;
        header.Product = "TimelineTest 1.0";
        header.Map = "Assets/Scenes/Arena.lvl";
        header.TickRate = 60;
        header.KeyframeInterval = 50;
        return header;
    }

    // 200 ticks, 4 chunks: client 1 from tick 0, client 2 joins at 20 and leaves at 150; a kill at 90;
    // a pick at 25; shots at 60 (a hit), 61 (a miss) and 62 (an arrow's sweep that hits: not a
    // trigger pull); client 2 misses commands at 100-101 and 105 (one run), and at 140; client 1's
    // clock resyncs at 120; the server is behind at 30, 31 and 45 (one hitch) and drops 3 at 180.
    std::vector<NetReplayTick> TimelineMatch()
    {
        std::vector<NetReplayTick> ticks;
        for( uint32_t t = 0; t < 200; ++t )
        {
            NetReplayTick tick;
            tick.Tick = t;
            tick.State.Tick = t;
            tick.State.Entities[65537].SetPose( Vector3( static_cast<float>( t ) * 0.1f, 1.f, 0.f ), Quaternion() );
            tick.State.Entities[65539].SetPose( Vector3( 0.f, 1.f, static_cast<float>( t ) * 0.1f ), Quaternion() );
            if( t == 0 )
            {
                tick.Entities.push_back( { 65537, "Striker", "Assets/Prefabs/Heroes/Striker.prefab", 1 } );
                tick.Entities.push_back( { 65539, "Bruiser", "Assets/Prefabs/Heroes/Bruiser.prefab", 2 } );
                tick.Events.push_back( { "net.joined", "{\"client\":1}", 0 } );
            }
            tick.TicksOwed = ( t == 30 || t == 31 || t == 45 ) ? 2 : 0;
            tick.TicksDropped = t == 180 ? 3 : 0;
            NetReplayClient& one = tick.Clients.emplace_back();
            one.Id = 1;
            one.RoundTripMs = 20.f + static_cast<float>( t % 10 );
            one.Slack = 2.f;
            one.Resyncs = t >= 120 ? 1 : 0;
            if( t >= 20 && t <= 150 )
            {
                NetReplayClient& two = tick.Clients.emplace_back();
                two.Id = 2;
                two.RoundTripMs = 80.f;
                two.PacketLoss = 0.05f;
                two.Missed = t == 100 || t == 101 || t == 105 || t == 140;
                two.Misses = t >= 140 ? 4 : t >= 105 ? 3 : t >= 101 ? 2 : t >= 100 ? 1 : 0;
            }
            if( t == 20 )
            {
                tick.Events.push_back( { "net.joined", "{\"client\":2}", 0 } );
            }
            if( t == 150 )
            {
                tick.Events.push_back( { "net.left", "{\"client\":2,\"reason\":\"left\"}", 0 } );
            }
            if( t == 25 )
            {
                tick.Events.push_back( { "pick", "{\"player\":\"Bravo\",\"hero\":\"Bruiser\"}", 65539 } );
            }
            if( t == 90 )
            {
                tick.Events.push_back( { "kill", "{\"killer\":\"Bravo\",\"victim\":\"Alpha\",\"headshot\":true}", 65537 } );
            }
            if( t >= 60 && t <= 62 )
            {
                NetReplayShot& shot = tick.Shots.emplace_back();
                shot.Client = 2;
                shot.Shooter = t == 62 ? 70000 : 65539;
                shot.Instigator = t == 62 ? 65539 : 0;
                shot.Hit = t != 61;
                shot.Target = shot.Hit ? 65537 : 0;
                shot.TargetPart = shot.Hit ? "Head" : "";
                shot.Direction = Vector3( 1.f, 0.f, 0.f );
                shot.Range = 50.f;
            }
            ticks.push_back( std::move( tick ) );
        }
        return ticks;
    }

    NetReplayMemorySource TimelineSource()
    {
        NetReplayMemorySource source( TimelineHeader() );
        const std::vector<NetReplayTick> ticks = TimelineMatch();
        for( size_t first = 0; first < ticks.size(); first += 50 )
        {
            std::vector<NetReplayTick> chunk( ticks.begin() + first, ticks.begin() + std::min( first + 50, ticks.size() ) );
            if( first > 0 )
            {
                // A chunk's first tick names every live NetId (the recorder does this).
                chunk.front().Entities = { { 65537, "Striker", "Assets/Prefabs/Heroes/Striker.prefab", 1 }, { 65539, "Bruiser", "Assets/Prefabs/Heroes/Bruiser.prefab", 2 } };
            }
            REQUIRE( source.AddChunk( NetReplayChunk::Encode( chunk, NetReplayCodec::Deflate ) ) );
        }
        return source;
    }
}


TEST_CASE( "Replay timeline: one pass gives kill / join / leave / event / hit markers, miss and hitch runs, resyncs, and each client's link per tick (ME-316)" )
{
    NetReplayMemorySource source = TimelineSource();
    NetReplayReader reader( source );
    NetReplayTimeline timeline;
    std::string error;
    REQUIRE_MESSAGE( timeline.Build( reader, &error ), error );
    CHECK( timeline.GetFirstTick() == 0 );
    CHECK( timeline.GetLastTick() == 199 );

    using Kind = NetReplayMarker::Kind;
    CHECK( timeline.Count( Kind::Kill ) == 1 );
    CHECK( timeline.Count( Kind::Join ) == 2 );
    CHECK( timeline.Count( Kind::Leave ) == 1 );
    CHECK( timeline.Count( Kind::Event ) == 1 );
    CHECK( timeline.Count( Kind::Hit ) == 1 );       // the sweep at 62 isn't a trigger pull, the miss at 61 isn't a hit
    CHECK( timeline.Count( Kind::Miss ) == 2 );      // 100-105 (merged), 140
    CHECK( timeline.Count( Kind::Resync ) == 1 );
    CHECK( timeline.Count( Kind::Hitch ) == 2 );     // 30-45 (merged), 180 (dropped)

    const std::vector<NetReplayMarker>& markers = timeline.GetMarkers();
    for( size_t i = 1; i < markers.size(); ++i )
    {
        CHECK( markers[i - 1].Tick <= markers[i].Tick );
    }
    const NetReplayMarker& kill = markers[timeline.Find( Kind::Kill, 0 )];
    CHECK( kill.Tick == 90 );
    CHECK( kill.Entity == 65537 );
    CHECK( kill.Label == "kill Bravo > Alpha [Striker#65537]" );
    const NetReplayMarker& join = markers[timeline.Find( Kind::Join, -1 )];
    CHECK( join.Tick == 20 );
    CHECK( join.Client == 2 );
    const NetReplayMarker& leave = markers[timeline.Find( Kind::Leave, 0 )];
    CHECK( leave.Tick == 150 );
    CHECK( leave.Client == 2 );
    const NetReplayMarker& hit = markers[timeline.Find( Kind::Hit, 0 )];
    CHECK( hit.Tick == 60 );
    CHECK( hit.Client == 2 );
    CHECK( hit.Entity == 65539 );
    CHECK( hit.Label == "client 2 Bruiser#65539 hit Striker#65537 (Head)" );
    const NetReplayMarker& miss = markers[timeline.Find( Kind::Miss, 0 )];
    CHECK( miss.Tick == 100 );
    CHECK( miss.Ticks == 6 );
    CHECK( miss.Client == 2 );
    CHECK( miss.Label.find( "for 3 tick(s) over 6 ticks" ) != std::string::npos );
    CHECK( markers[timeline.Find( Kind::Miss, 1 )].Tick == 140 );
    const NetReplayMarker& resync = markers[timeline.Find( Kind::Resync, 0 )];
    CHECK( resync.Tick == 120 );
    CHECK( resync.Client == 1 );
    const NetReplayMarker& hitch = markers[timeline.Find( Kind::Hitch, 0 )];
    CHECK( hitch.Tick == 30 );
    CHECK( hitch.Ticks == 16 );
    CHECK( hitch.Label == "hitch: the server was behind 3 tick(s) over 16 ticks (up to 2 owed)" );
    CHECK( markers[timeline.Find( Kind::Hitch, -1 )].Label == "hitch: the server was behind 1 tick(s) (up to 0 owed, 3 dropped)" );
    CHECK( timeline.Find( Kind::Kill, 1 ) == -1 );
    CHECK( timeline.Find( Kind::Kill, -2 ) == -1 );
    CHECK( timeline.Find( Kind::Event, 0, "pick" ) >= 0 );
    CHECK( timeline.Find( Kind::Event, 0, "nope" ) == -1 );

    // Next / previous from a tick, by kind.
    const uint32_t killsOnly = 1u << static_cast<uint32_t>( Kind::Kill );
    CHECK( markers[timeline.Neighbour( 0, true, killsOnly )].Tick == 90 );
    CHECK( timeline.Neighbour( 90, true, killsOnly ) == -1 );
    CHECK( markers[timeline.Neighbour( 150, false, killsOnly )].Tick == 90 );
    CHECK( markers[timeline.Neighbour( 90, true )].Tick == 100 );
    CHECK( markers[timeline.Neighbour( 90, false )].Tick == 60 );

    // Each client's link, a value per tick.
    REQUIRE( timeline.GetClients().size() == 2 );
    const NetReplayClientSeries& one = timeline.GetClients()[0];
    const NetReplayClientSeries& two = timeline.GetClients()[1];
    CHECK( one.Client == 1 );
    CHECK( one.FirstTick == 0 );
    CHECK( one.LastTick == 199 );
    REQUIRE( one.RoundTripMs.size() == 200 );
    CHECK( one.RoundTripMs[37] == doctest::Approx( 27.f ) );
    CHECK( one.MaxRoundTripMs == doctest::Approx( 29.f ) );
    CHECK( one.Resyncs == 1 );
    CHECK( two.Client == 2 );
    CHECK( two.FirstTick == 20 );
    CHECK( two.LastTick == 150 );
    REQUIRE( two.RoundTripMs.size() == 180 );
    CHECK( two.RoundTripMs[0] == doctest::Approx( 80.f ) );
    CHECK( std::isnan( two.RoundTripMs[131] ) );    // tick 151: gone
    CHECK( two.PacketLoss[10] == doctest::Approx( 0.05f ) );
    CHECK( two.Missed[80] == 1 );
    CHECK( two.Missed[82] == 0 );
    CHECK( two.Misses == 4 );
    CHECK( timeline.GetTicksOwed()[31] == 2 );
    CHECK( timeline.GetShots()[60] == 1 );
    CHECK( timeline.GetShots()[62] == 0 );

    // Cancelled: false, no error.
    std::atomic<bool> cancel{ true };
    NetReplayTimeline stopped;
    std::string none;
    CHECK_FALSE( stopped.Build( reader, &none, &cancel ) );
    CHECK( none.empty() );

    NetReplayMarker::Kind parsed = Kind::Event;
    CHECK( NetReplayTimeline::ParseKind( "hitch", parsed ) );
    CHECK( parsed == Kind::Hitch );
    CHECK_FALSE( NetReplayTimeline::ParseKind( "explosion", parsed ) );
}


TEST_CASE( "Replay timeline: ReplayDump --markers prints them as text and JSON, within --ticks and --entity (ME-316)" )
{
    NetReplayMemorySource source = TimelineSource();
    NetReplayReader reader( source );
    auto dump = [&reader]( std::vector<std::string> InArgs ) {
        InArgs.insert( InArgs.begin(), { "Player", "--replay-dump", "memory.mreplay" } );
        NetReplayDump::Options options;
        std::string error;
        REQUIRE_MESSAGE( NetReplayDump::ParseArgs( InArgs, options, error ), error );
        std::ostringstream out;
        REQUIRE_MESSAGE( NetReplayDump::Write( reader, options, out, error ), error );
        return out.str();
    };
    const std::string text = dump( { "--markers" } );
    CHECK( text.find( "90 marker kill kill Bravo > Alpha [Striker#65537]\n" ) != std::string::npos );
    CHECK( text.find( "60 marker hit client 2 Bruiser#65539 hit Striker#65537 (Head)\n" ) != std::string::npos );
    CHECK( text.find( "markers: kill 1 join 2 leave 1 event 1 hit 1 miss 2 resync 1 hitch 2 (whole replay)" ) != std::string::npos );
    // --markers alone prints no tick sections.
    CHECK( text.find( " shot " ) == std::string::npos );

    const json document = json::parse( dump( { "--markers", "--json", "--ticks", "80:130" } ) );
    REQUIRE( document.contains( "markers" ) );
    std::vector<std::string> kinds;
    for( const json& marker : document["markers"] )
    {
        kinds.push_back( marker["kind"].get<std::string>() );
    }
    CHECK( kinds == std::vector<std::string>{ "kill", "miss", "resync" } );
    CHECK( document["markers"][0]["tick"] == 90 );
    CHECK( document["markers"][0]["entity"] == 65537 );
    CHECK( document["markers"][1]["ticks"] == 6 );

    const json entity = json::parse( dump( { "--markers", "--json", "--entity", "65539" } ) );
    REQUIRE( entity["markers"].size() == 2 );   // the pick and the hit (the shooter)
    CHECK( entity["markers"][0]["kind"] == "event" );
    CHECK( entity["markers"][1]["kind"] == "hit" );
}
