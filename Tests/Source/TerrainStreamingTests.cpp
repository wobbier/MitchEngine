#include <doctest/doctest.h>
#include "Terrain/TerrainStreamCache.h"
#include "Terrain/TerrainStreamScheduler.h"
#include "Terrain/TerrainLod.h"
#include <filesystem>
#include <fstream>
#include <cmath>
#include <future>
#include <atomic>

namespace
{
    struct StreamFixture
    {
        std::filesystem::path Directory, File;
        TerrainData::Heightfield Field;
        std::shared_ptr<const TerrainData::StreamArchive> Archive;
        StreamFixture() : Field( Settings() )
        {
            static uint64_t sequence = 0;
            Directory = std::filesystem::path( ".tmp/Tests/TerrainStreaming" ) / std::to_string( ++sequence );
            std::filesystem::remove_all( Directory ); std::filesystem::create_directories( Directory ); File = Directory / "Runtime.terrainc";
            std::string error;
            for( int x = -1; x < 2; ++x ) for( int z = -1; z < 2; ++z ) REQUIRE( Field.CreateFlatTile( { x, z }, 0, error ) );
            for( int z = -8; z <= 16; ++z ) for( int x = -8; x <= 16; ++x ) REQUIRE( Field.SetSample( { x, z }, float( 2 * std::sin( x * 0.4 ) * std::cos( z * 0.3 ) ), error ) );
            REQUIRE( TerrainData::CookStreamArchive( File, Field, 123, 1, error ) );
            REQUIRE( TerrainData::OpenStreamArchive( File, Archive, error ) );
        }
        static TerrainData::Settings Settings()
        {
            TerrainData::Settings s; s.TileQuads = 8; s.PatchQuads = 4; s.MinTile = { -1, -1 }; s.TilesX = s.TilesZ = 3; return s;
        }
    };

    void Drain( TerrainData::TerrainStreamCache& cache, std::span<const TerrainData::TileDemand> demand, const TerrainData::StreamLimits& limits, uint64_t& frame )
    {
        for( int i = 0; i < 40; ++i )
        {
            cache.Tick( ++frame, demand, limits ); cache.WaitForRequest();
            if( !cache.GetStats().Loading && cache.GetStats().Pending == 0 ) break;
        }
    }

    void Flip( const std::filesystem::path& path, uint64_t offset )
    {
        std::fstream file( path, std::ios::binary | std::ios::in | std::ios::out ); file.seekg( offset ); char value; file.read( &value, 1 );
        value ^= 0x20; file.seekp( offset ); file.write( &value, 1 );
    }
}

TEST_CASE( "Terrain streaming: merged world demand preserves a camera exactly at a tile center" )
{
    const auto settings = StreamFixture::Settings(); std::map<TerrainData::TileCoord, TerrainData::TileDemand> demand;
    TerrainData::AddStreamDemand( settings, Vector3( 4, 0, 4 ), 0, false, demand );
    TerrainData::AddStreamDemand( settings, Vector3( 6, 0, 6 ), 0, false, demand );
    TerrainData::AddStreamDemand( settings, Vector3( 7, 0, 7 ), 0, true, demand );
    REQUIRE( demand.size() == 1 ); CHECK( demand.begin()->second.Priority == 0 ); CHECK( demand.begin()->second.Visual ); CHECK( demand.begin()->second.Collision );
}

TEST_CASE( "Terrain streaming: world scheduling ranks collision across assets before optional detail" )
{
    StreamFixture a, b; TerrainData::TerrainStreamCache first( a.Archive ), second( b.Archive );
    const auto cost = TerrainData::TerrainStreamCache::TileReservation( a.Field.GetSettings() );
    TerrainData::StreamWorldLimits limits;
    limits.CpuBytes = first.GetStats().CpuReserved + second.GetStats().CpuReserved + 2 * ( a.Archive->Coverage->Patches.size() * 128 + 4096 ) + cost.CpuBytes + a.Field.GetSettings().SamplesPerTile() * 16 + 1024;
    limits.CollisionBytes = cost.CollisionBytes; limits.MaxLoads = 1;
    TerrainData::StreamBatch batches[] = { { &first, {}, { { { -1, -1 }, false, true, 1 } } }, { &second, {}, { { { 1, 1 }, true, false, 10000 } } } };
    TerrainData::TerrainStreamScheduler scheduler;
    scheduler.Tick( 1, batches, limits );
    CHECK_FALSE( first.GetStats().Loading ); REQUIRE( second.GetStats().Loading ); CHECK( scheduler.GetStats().Loading == 1 );
    for( uint64_t frame = 2; frame < 30; ++frame )
    {
        first.WaitForRequest(); second.WaitForRequest(); scheduler.Tick( frame, batches, limits );
        CHECK( scheduler.GetStats().CpuReserved <= limits.CpuBytes ); CHECK( scheduler.GetStats().CollisionReserved <= limits.CollisionBytes );
    }
    CHECK( first.GetStats().Loaded == 0 ); CHECK( second.GetStats().Loaded == 1 ); CHECK( second.GetStats().CollisionReady );
    REQUIRE( second.GetData()->CollisionTiles.size() == 1 ); CHECK( scheduler.GetStats().Deferred == 1 );
    // A lower cap keeps the required demand in the readiness calculation instead of dropping it.
    limits.CollisionBytes = 0; batches[1].Demand[0].Coordinate = { 0, 0 }; scheduler.Tick( 31, batches, limits ); CHECK_FALSE( scheduler.GetStats().CollisionReady );
}

TEST_CASE( "Terrain streaming: world GPU handoff waits for another asset's retiring pins" )
{
    StreamFixture a, b; TerrainData::TerrainStreamCache first( a.Archive ), second( b.Archive );
    const auto cost = TerrainData::TerrainStreamCache::TileReservation( a.Field.GetSettings() );
    TerrainData::StreamWorldLimits limits; limits.GpuBytes = first.GetStats().GpuReserved + second.GetStats().GpuReserved + cost.GpuBytes; limits.MaxLoads = 1;
    TerrainData::StreamBatch batches[] = { { &first, {}, { { { -1, -1 }, false, true, 100 } } }, { &second, {}, { { { 1, 1 }, false, true, 1 } } } };
    TerrainData::TerrainStreamScheduler scheduler;
    uint64_t frame = 0;
    const auto step = [&]() { first.WaitForRequest(); second.WaitForRequest(); scheduler.Tick( ++frame, batches, limits ); CHECK( scheduler.GetStats().GpuReserved <= limits.GpuBytes ); CHECK( scheduler.GetStats().Loading <= 1 ); };
    for( int n = 0; n < 20; ++n ) step();
    REQUIRE( second.GetStats().Detail == 1 ); CHECK( first.GetStats().Loaded == 0 ); auto pin = second.GetData();
    batches[0].Demand[0].Priority = 0; batches[1].Active = false;
    for( int n = 0; n < 10; ++n ) step();
    CHECK( first.GetStats().Detail == 0 ); CHECK( second.GetStats().Retiring == 1 ); CHECK_FALSE( second.CanRetire() ); CHECK( scheduler.GetStats().RetiringAssets == 1 );
    CHECK( pin->Heights->Query( 10, 10 ).Status == TerrainData::QueryStatus::Ready ); pin.reset();
    for( int n = 0; n < 20; ++n ) step();
    CHECK( first.GetStats().Detail == 1 ); CHECK( second.CanRetire() ); CHECK( second.GetStats().Evicted == 1 );
}

TEST_CASE( "Terrain streaming: world admission respects asset ceilings and reports an oversized coverage floor" )
{
    StreamFixture a, b; TerrainData::TerrainStreamCache first( a.Archive ), second( b.Archive );
    TerrainData::StreamBatch batches[] = { { &first, {}, { { { 0, 0 }, false, true, 0 } } }, { &second, {}, { { { 0, 0 }, false, true, 1 } } } };
    batches[0].Limits.GpuBytes = first.GetStats().GpuReserved;
    TerrainData::StreamWorldLimits limits; limits.MaxLoads = 1; TerrainData::TerrainStreamScheduler scheduler;
    for( uint64_t frame = 0; frame < 20; ++frame ) { first.WaitForRequest(); second.WaitForRequest(); scheduler.Tick( frame, batches, limits ); }
    CHECK( first.GetStats().Loaded == 0 ); CHECK( second.GetStats().Detail == 1 );
    limits.CpuBytes = 1; limits.GpuBytes = 1; scheduler.Tick( 21, batches, limits );
    CHECK( scheduler.GetStats().OverBudget ); CHECK_FALSE( scheduler.GetStats().Pressure.empty() );
    CHECK_FALSE( first.GetStats().CoverageReady ); CHECK_FALSE( second.GetStats().CoverageReady ); CHECK( scheduler.GetStats().Loading == 0 );
}

TEST_CASE( "Terrain streaming: independent detail blocks match source geometry, normals and canonical queries" )
{
    StreamFixture f; std::string error;
    CHECK( f.Archive->Coverage->Heights->GetTiles().empty() ); CHECK( f.Archive->Coverage->CollisionTiles.empty() );
    CHECK( f.Archive->Coverage->Patches.size() == 36 );
    for( const auto& block : f.Archive->Blocks )
    {
        std::shared_ptr<const TerrainData::StreamTile> tile;
        REQUIRE( TerrainData::ReadStreamTile( *f.Archive, block.Coordinate, tile, error ) );
        REQUIRE( tile->Heights == f.Field.FindTile( block.Coordinate )->Heights );
        for( size_t i = 0; i < tile->Patches.size(); ++i )
        {
            TerrainData::PatchMesh expected; REQUIRE( TerrainData::BuildPatch( f.Field, block.Coordinate, uint32_t( i % 2 ), uint32_t( i / 2 ), expected, error ) );
            const auto& actual = tile->Patches[i]->Mesh;
            REQUIRE( actual.Vertices.size() == expected.Vertices.size() );
            for( size_t n = 0; n < actual.Vertices.size(); ++n )
            {
                CHECK( actual.Vertices[n].Position.InternalVector == expected.Vertices[n].Position.InternalVector );
                CHECK( actual.Vertices[n].Normal.InternalVector == expected.Vertices[n].Normal.InternalVector );
            }
            for( size_t l = 0; l < expected.Lods.size(); ++l ) { CHECK( actual.Lods[l].Indices == expected.Lods[l].Indices ); CHECK( actual.Lods[l].MaxError == expected.Lods[l].MaxError ); }
            const auto& coarse = f.Archive->Coverage->Patches[f.Archive->TileIndex( block.Coordinate ) * 4 + i];
            CHECK( coarse->BaseLod == 1 ); CHECK( coarse->Mesh.Lods.front().MaxError == expected.Lods[1].MaxError );
        }
    }
}

TEST_CASE( "Terrain streaming: corruption is isolated, generations are checked and failed cooks preserve publication" )
{
    StreamFixture f; std::string error; auto original = f.Archive;
    Flip( f.File, f.Archive->Blocks[4].Offset + 17 );
    std::shared_ptr<const TerrainData::StreamArchive> archive;
    REQUIRE( TerrainData::OpenStreamArchive( f.File, archive, error ) ); // No detail is read at open.
    std::shared_ptr<const TerrainData::StreamTile> tile;
    CHECK_FALSE( TerrainData::ReadStreamTile( *archive, { 0, 0 }, tile, error ) ); CHECK( tile == nullptr );
    REQUIRE( TerrainData::ReadStreamTile( *archive, { -1, -1 }, tile, error ) ); auto retained = tile;
    CHECK_FALSE( TerrainData::ReadStreamTile( *archive, { 0, 0 }, tile, error ) ); CHECK( tile == retained );
    CHECK_FALSE( TerrainData::CookStreamArchive( f.File, f.Field, 123, 9, error ) );
    REQUIRE( TerrainData::OpenStreamArchive( f.File, archive, error ) ); CHECK( archive->Header == original->Header );
    REQUIRE( f.Field.SetSample( { 1, 1 }, 7, error ) ); REQUIRE( TerrainData::CookStreamArchive( f.File, f.Field, 123, 1, error ) );
    CHECK_FALSE( TerrainData::ReadStreamTile( *original, { -1, -1 }, tile, error ) ); CHECK( tile == retained );
    Flip( f.File, 12 ); CHECK_FALSE( TerrainData::OpenStreamArchive( f.File, archive, error ) );
}

TEST_CASE( "Terrain streaming: cache requests collision first and keeps GPU and collision admission independent" )
{
    StreamFixture f; TerrainData::TerrainStreamCache cache( f.Archive ); auto initial = cache.GetStats();
    TerrainData::StreamLimits limits; limits.HoldFrames = 0; limits.GpuBytes = initial.GpuReserved;
    const auto tileCost = TerrainData::TerrainStreamCache::TileReservation( f.Field.GetSettings() );
    limits.CpuBytes = initial.CpuReserved + tileCost.CpuBytes * 4 + 65536;
    limits.CollisionBytes = tileCost.CollisionBytes * 2;
    TerrainData::TileDemand requests[] = { { { 1, 1 }, false, true, 0 }, { { -1, -1 }, true, false, 10000 } };
    uint64_t frame = 0; Drain( cache, requests, limits, frame );
    CHECK( cache.GetStats().Loaded == 2 ); CHECK( cache.GetStats().CollisionReady ); CHECK( cache.GetStats().Detail == 0 );
    REQUIRE( cache.GetData()->CollisionTiles.size() == 1 ); CHECK( cache.GetData()->CollisionTiles.front()->Coordinate == TerrainData::TileCoord{ -1, -1 } );
    CHECK( cache.GetData()->Heights->Query( -3, -3 ).Status == TerrainData::QueryStatus::Ready );
    CHECK( cache.GetStats().GpuReserved <= limits.GpuBytes ); CHECK( cache.GetStats().CpuReserved <= limits.CpuBytes ); CHECK( cache.GetStats().CollisionReserved <= limits.CollisionBytes );
}

TEST_CASE( "Terrain streaming: retiring snapshots pin reservations and eviction releases them" )
{
    StreamFixture f; TerrainData::TerrainStreamCache cache( f.Archive ); auto baseline = cache.GetStats(); auto cost = TerrainData::TerrainStreamCache::TileReservation( f.Field.GetSettings() );
    TerrainData::StreamLimits limits; limits.HoldFrames = 0; limits.CpuBytes = baseline.CpuReserved + cost.CpuBytes + 16000;
    TerrainData::TileDemand request{ { -1, -1 }, false, true, 0 }; uint64_t frame = 0; Drain( cache, { &request, 1 }, limits, frame );
    REQUIRE( cache.GetStats().Resident == 1 ); auto pin = cache.GetData();
    request.Coordinate = { 1, 1 };
    for( int n = 0; n < 4; ++n ) { cache.Tick( ++frame, { &request, 1 }, limits ); cache.WaitForRequest(); }
    CHECK( cache.GetStats().Retiring == 1 ); CHECK( cache.GetStats().Loaded == 1 ); CHECK_FALSE( cache.GetStats().Pressure.empty() );
    CHECK( pin->Heights->Query( -3, -3 ).Status == TerrainData::QueryStatus::Ready ); pin.reset();
    Drain( cache, { &request, 1 }, limits, frame );
    CHECK( cache.GetStats().Evicted >= 1 ); CHECK( cache.GetStats().Loaded == 2 ); CHECK( cache.GetStats().CpuReserved <= limits.CpuBytes );
    CHECK( cache.GetData()->Heights->Query( -3, -3 ).Status == TerrainData::QueryStatus::Unavailable );
}

TEST_CASE( "Terrain streaming: cancelled completions cannot republish a teleported region" )
{
    StreamFixture f; auto started = std::make_shared<std::promise<void>>(); auto release = std::make_shared<std::promise<void>>(); auto gate = release->get_future().share();
    auto first = std::make_shared<std::atomic<bool>>( true );
    TerrainData::TerrainStreamCache cache( f.Archive, [started, gate, first]( const auto& archive, auto coord, auto& tile, auto& error ) {
        if( first->exchange( false ) ) { started->set_value(); gate.wait(); }
        return TerrainData::ReadStreamTile( archive, coord, tile, error );
    } );
    TerrainData::StreamLimits limits; limits.HoldFrames = 0; TerrainData::TileDemand request{ { -1, -1 }, false, true, 0 }; uint64_t frame = 1;
    cache.Tick( frame, { &request, 1 }, limits ); started->get_future().wait(); request.Coordinate = { 1, 1 };
    cache.Tick( ++frame, { &request, 1 }, limits ); release->set_value(); cache.WaitForRequest(); Drain( cache, { &request, 1 }, limits, frame );
    CHECK( cache.GetStats().Cancelled == 1 ); CHECK( cache.GetStats().Loaded == 1 );
    CHECK( cache.GetData()->Heights->Query( -3, -3 ).Status == TerrainData::QueryStatus::Unavailable ); CHECK( cache.GetData()->Heights->Query( 10, 10 ).Status == TerrainData::QueryStatus::Ready );
}

TEST_CASE( "Terrain streaming: unavailable detail constrains neighboring LODs without internal cracks" )
{
    StreamFixture f; TerrainData::TerrainStreamCache cache( f.Archive ); TerrainData::StreamLimits limits;
    TerrainData::TileDemand request{ { 0, 0 }, false, true, 0 }; uint64_t frame = 0; Drain( cache, { &request, 1 }, limits, frame );
    auto data = cache.GetData(); TerrainData::LodView view; view.Position = Vector3( 0, 5, -20 ); view.PixelsX = view.PixelsY = 10000;
    TerrainData::LodSettings settings; std::vector<TerrainData::PatchLodChoice> choices; std::string error;
    REQUIRE( TerrainData::SelectTerrainLods( *data, {}, { &view, 1 }, settings, {}, choices, error ) );
    bool fine = false, coarse = false, stitched = false;
    for( size_t i = 0; i < choices.size(); ++i )
    {
        CHECK( choices[i].Level >= data->Patches[i]->BaseLod ); fine |= data->Patches[i]->BaseLod == 0; coarse |= data->Patches[i]->BaseLod > 0; stitched |= choices[i].Edges != 0;
        for( int32_t n : data->PatchNeighbors[i] ) if( n >= 0 ) CHECK( std::abs( int( choices[i].Level ) - int( choices[n].Level ) ) <= 1 );
    }
    CHECK( fine ); CHECK( coarse ); CHECK( stitched );
}

TEST_CASE( "Terrain streaming: a lifecycle cancellation requeues still-wanted detail without publishing the old completion" )
{
    StreamFixture f; auto started = std::make_shared<std::promise<void>>(); auto release = std::make_shared<std::promise<void>>(); auto gate = release->get_future().share();
    auto calls = std::make_shared<std::atomic<int>>( 0 );
    TerrainData::TerrainStreamCache cache( f.Archive, [started, gate, calls]( const auto& archive, auto coord, auto& tile, auto& error ) {
        if( calls->fetch_add( 1 ) == 0 ) { started->set_value(); gate.wait(); }
        return TerrainData::ReadStreamTile( archive, coord, tile, error );
    } );
    TerrainData::StreamLimits limits; TerrainData::TileDemand request{ { 0, 0 }, false, true, 0 }; uint64_t frame = 1;
    cache.Tick( frame, { &request, 1 }, limits ); started->get_future().wait(); cache.InvalidatePending();
    cache.Tick( ++frame, { &request, 1 }, limits ); release->set_value(); cache.WaitForRequest(); Drain( cache, { &request, 1 }, limits, frame );
    CHECK( calls->load() == 2 ); CHECK( cache.GetStats().Cancelled == 1 ); CHECK( cache.GetStats().Loaded == 1 ); CHECK( cache.GetStats().Pending == 0 );
}

TEST_CASE( "Terrain streaming: failed detail retains coverage and never retries every frame" )
{
    StreamFixture f; Flip( f.File, f.Archive->Blocks[4].Offset + 8 ); TerrainData::TerrainStreamCache cache( f.Archive );
    TerrainData::StreamLimits limits; TerrainData::TileDemand request{ { 0, 0 }, true, true, 0 }; uint64_t frame = 0;
    Drain( cache, { &request, 1 }, limits, frame ); const auto reads = cache.GetStats().Failed;
    for( int n = 0; n < 50; ++n ) cache.Tick( ++frame, { &request, 1 }, limits );
    CHECK( reads == 1 ); CHECK( cache.GetStats().Failed == 1 ); CHECK_FALSE( cache.GetStats().Loading ); CHECK_FALSE( cache.GetStats().CollisionReady );
    CHECK( cache.GetData()->Patches.size() == f.Archive->Coverage->Patches.size() ); CHECK_FALSE( cache.GetStats().Error.empty() );
    CHECK( cache.GetData()->Heights->Query( 2, 2 ).Status == TerrainData::QueryStatus::Unavailable );
}

TEST_CASE( "Terrain streaming: CPU-ready publication retries after budget pressure with unchanged demand" )
{
    StreamFixture f; TerrainData::TerrainStreamCache cache( f.Archive ); TerrainData::StreamLimits limits;
    TerrainData::TileDemand request{ { 0, 0 }, true, true, 0 }; uint64_t frame = 1;
    cache.Tick( frame, { &request, 1 }, limits ); cache.WaitForRequest();
    auto tight = limits; tight.CpuBytes = cache.GetStats().CpuReserved;
    cache.Tick( ++frame, { &request, 1 }, tight );
    REQUIRE( cache.GetStats().Loaded == 1 ); CHECK_FALSE( cache.GetStats().CollisionReady );
    cache.Tick( ++frame, { &request, 1 }, limits );
    CHECK( cache.GetStats().CollisionReady ); CHECK( cache.GetStats().Loaded == 1 );
    CHECK( cache.GetData()->Heights->Query( 2, 2 ).Status == TerrainData::QueryStatus::Ready );
}

TEST_CASE( "Terrain streaming: GPU detail can leave an actor's tile while its canonical collision stays pinned" )
{
    StreamFixture f; TerrainData::TerrainStreamCache cache( f.Archive ); TerrainData::StreamLimits limits; limits.HoldFrames = 0;
    const auto cost = TerrainData::TerrainStreamCache::TileReservation( f.Field.GetSettings() ); limits.GpuBytes = cache.GetStats().GpuReserved + cost.GpuBytes;
    TerrainData::TileDemand demand[] = { { { 0, 0 }, true, true, 0 }, { { 1, 1 }, false, true, 100 } }; uint64_t frame = 0;
    Drain( cache, demand, limits, frame ); REQUIRE( cache.GetStats().Detail == 1 ); REQUIRE( cache.GetStats().CollisionReady );
    demand[0].Visual = false; demand[1].Priority = 0;
    for( int n = 0; n < 8; ++n ) { cache.Tick( ++frame, demand, limits ); cache.WaitForRequest(); }
    REQUIRE( cache.GetStats().CollisionReady ); CHECK( cache.GetStats().GpuReserved <= limits.GpuBytes );
    const auto data = cache.GetData(); CHECK( data->Patches[f.Archive->TileIndex( { 0, 0 } ) * 4]->BaseLod == 1 );
    CHECK( data->Patches[f.Archive->TileIndex( { 1, 1 } ) * 4]->BaseLod == 0 );
    REQUIRE( data->CollisionTiles.size() == 1 ); CHECK( data->CollisionTiles[0]->Coordinate == TerrainData::TileCoord{ 0, 0 } );
}
