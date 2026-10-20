#include <doctest/doctest.h>
#include "Terrain/TerrainStreamCache.h"
#include "Terrain/TerrainStreamScheduler.h"
#include "Terrain/TerrainLod.h"
#include "Terrain/TerrainSurface.h"
#include <filesystem>
#include <fstream>
#include <cmath>
#include <future>
#include <atomic>
#include <bit>

namespace
{
    void PageFixture( const std::filesystem::path& file, const TerrainData::Heightfield& field, std::shared_ptr<const TerrainData::StreamArchive>& archive )
    {
        std::string error; REQUIRE( TerrainData::CookPagedStreamArchive( file, field, 123, 1, error ) ); REQUIRE( TerrainData::OpenStreamArchive( file, archive, error ) );
    }

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

TEST_CASE( "Terrain paging: metadata opens without overview geometry and independent coverage matches version 2" )
{
    StreamFixture f; auto legacy = f.Archive; PageFixture( f.File, f.Field, f.Archive ); std::string error;
    REQUIRE( f.Archive->PagedCoverage ); CHECK( f.Archive->Coverage->Patches.empty() ); CHECK( f.Archive->Coverage->Heights->GetTiles().empty() );
    CHECK( f.Archive->Coverage->Bounds.Min.InternalVector == legacy->Coverage->Bounds.Min.InternalVector );
    CHECK( f.Archive->Coverage->Bounds.Max.InternalVector == legacy->Coverage->Bounds.Max.InternalVector );
    for( const auto& block : f.Archive->Blocks )
    {
        std::shared_ptr<const TerrainData::StreamCoverageTile> page; REQUIRE( TerrainData::ReadStreamCoverage( *f.Archive, block.Coordinate, page, error ) ); REQUIRE( page->Patches.size() == 4 );
        for( size_t i = 0; i < 4; ++i )
        {
            const auto& actual = page->Patches[i]->Mesh; const auto& expected = legacy->Coverage->Patches[f.Archive->TileIndex( block.Coordinate ) * 4 + i]->Mesh;
            REQUIRE( actual.Vertices.size() == expected.Vertices.size() );
            for( size_t v = 0; v < actual.Vertices.size(); ++v ) { CHECK( actual.Vertices[v].Position.InternalVector == expected.Vertices[v].Position.InternalVector ); CHECK( actual.Vertices[v].Normal.InternalVector == expected.Vertices[v].Normal.InternalVector ); }
            for( size_t l = 0; l < actual.Lods.size(); ++l ) { CHECK( actual.Lods[l].Indices == expected.Lods[l].Indices ); CHECK( actual.Lods[l].MaxError == expected.Lods[l].MaxError ); }
        }
        std::shared_ptr<const TerrainData::StreamTile> detail; REQUIRE( TerrainData::ReadStreamTile( *f.Archive, block.Coordinate, detail, error ) ); CHECK( detail->Heights == f.Field.FindTile( block.Coordinate )->Heights );
    }
    Flip( f.File, f.Archive->Blocks[4].CoverageOffset + 17 );
    std::shared_ptr<const TerrainData::StreamCoverageTile> page; CHECK_FALSE( TerrainData::ReadStreamCoverage( *f.Archive, { 0, 0 }, page, error ) );
    REQUIRE( TerrainData::ReadStreamCoverage( *f.Archive, { -1, -1 }, page, error ) ); const auto retained = page;
    CHECK_FALSE( TerrainData::ReadStreamCoverage( *f.Archive, { 0, 0 }, page, error ) ); CHECK( page == retained );
    std::shared_ptr<const TerrainData::StreamTile> detail; REQUIRE( TerrainData::ReadStreamTile( *f.Archive, { 0, 0 }, detail, error ) ); // Collision does not depend on the coarse page.
    auto archive = f.Archive; CHECK_FALSE( TerrainData::CookPagedStreamArchive( f.File, f.Field, 123, 10, error ) );
    REQUIRE( TerrainData::OpenStreamArchive( f.File, f.Archive, error ) ); CHECK( f.Archive->Header == archive->Header );
    REQUIRE( f.Field.SetSample( { 1, 1 }, 7, error ) ); REQUIRE( TerrainData::CookPagedStreamArchive( f.File, f.Field, 123, 1, error ) );
    CHECK_FALSE( TerrainData::ReadStreamCoverage( *archive, { -1, -1 }, page, error ) ); CHECK( page == retained );
    REQUIRE( TerrainData::OpenStreamArchive( f.File, f.Archive, error ) ); const auto retainedArchive = f.Archive;
    auto header = f.Archive->Header; const uint32_t spacing = std::bit_cast<uint32_t>( 3e37f );
    for( int i = 0; i < 4; ++i ) header[16 + i] = static_cast<uint8_t>( spacing >> ( i * 8 ) );
    uint64_t hash = 14695981039346656037ull; for( size_t i = 0; i < 96; ++i ) hash = ( hash ^ header[i] ) * 1099511628211ull;
    for( int i = 0; i < 8; ++i ) header[96 + i] = static_cast<uint8_t>( hash >> ( i * 8 ) );
    { std::fstream file( f.File, std::ios::binary | std::ios::in | std::ios::out ); file.write( reinterpret_cast<const char*>( header.data() ), header.size() ); }
    CHECK_FALSE( TerrainData::OpenStreamArchive( f.File, f.Archive, error ) ); CHECK( f.Archive == retainedArchive ); // Metadata bounds must remain finite before any page is read.
}

TEST_CASE( "Terrain paging: coverage travels within shared budgets and retired query snapshots keep their charge" )
{
    StreamFixture f; PageFixture( f.File, f.Field, f.Archive ); TerrainData::TerrainStreamCache cache( f.Archive );
    const auto page = TerrainData::TerrainStreamCache::CoverageReservation( f.Field.GetSettings(), 1 );
    TerrainData::StreamWorldLimits limits; limits.CpuBytes = cache.GetStats().CpuReserved + page.CpuBytes * 6 + TerrainData::TerrainStreamCache::TileReservation( f.Field.GetSettings() ).CpuBytes + 65536;
    limits.GpuBytes = cache.GetStats().GpuReserved + page.GpuBytes * 4; limits.MaxLoads = 1;
    TerrainData::StreamBatch batch{ &cache }; batch.Limits.HoldFrames = 0;
    for( int x = -1; x <= 0; ++x ) for( int z = -1; z <= 0; ++z ) batch.Demand.push_back( { { x, z }, x == 0 && z == 0, false, double( x * x + z * z ), true } );
    TerrainData::TerrainStreamScheduler scheduler; uint64_t frame = 0;
    const auto step = [&]() { cache.WaitForRequest(); scheduler.Tick( ++frame, { &batch, 1 }, limits ); CHECK( scheduler.GetStats().CpuReserved <= limits.CpuBytes ); CHECK( scheduler.GetStats().GpuReserved <= limits.GpuBytes ); };
    for( int n = 0; n < 35; ++n ) step();
    REQUIRE( cache.GetData()->CoverageTiles.size() == 4 ); CHECK( cache.GetData()->Patches.size() == 16 ); CHECK( cache.GetStats().HorizonReady ); CHECK( cache.GetStats().CollisionReady );
    CHECK( cache.GetData()->Heights->Query( -3, -3 ).Status == TerrainData::QueryStatus::Unavailable ); CHECK( cache.GetData()->Heights->Query( 3, 3 ).Status == TerrainData::QueryStatus::Ready );
    auto pin = cache.GetData(); batch.Demand.clear();
    for( int x = 0; x <= 1; ++x ) for( int z = 0; z <= 1; ++z ) batch.Demand.push_back( { { x, z }, x == 0 && z == 0, false, double( x * x + z * z ), true } );
    for( int n = 0; n < 10; ++n ) step();
    CHECK( cache.GetStats().CoverageRetiring == 3 ); CHECK( cache.GetStats().CoverageLoaded == 4 ); CHECK_FALSE( cache.GetStats().HorizonReady ); pin.reset();
    for( int n = 0; n < 35; ++n ) step();
    REQUIRE( cache.GetData()->CoverageTiles.size() == 4 ); CHECK( cache.GetStats().CoverageLoaded == 7 ); CHECK( cache.GetStats().CoverageEvicted >= 3 ); CHECK( cache.GetStats().HorizonReady );
    TerrainData::LodSettings lod; lod.ForcedLevel = 0; std::vector<TerrainData::PatchLodChoice> choices; std::string error;
    REQUIRE( TerrainData::SelectTerrainLods( *cache.GetData(), {}, {}, lod, {}, choices, error ) ); for( const auto& choice : choices ) CHECK( choice.Level >= 1 );
    batch.Demand = { { { 0, 0 }, true, false, 0 } }; for( int n = 0; n < 20; ++n ) step();
    CHECK( cache.GetData()->Patches.empty() ); CHECK( cache.GetStats().CollisionReady ); CHECK( cache.GetStats().CoverageResident == 0 ); CHECK( cache.GetData()->CollisionTiles.size() == 1 );
}

TEST_CASE( "Terrain paging: coverage failures and cancellation preserve canonical collision independence" )
{
    StreamFixture f; PageFixture( f.File, f.Field, f.Archive ); Flip( f.File, f.Archive->Blocks[4].CoverageOffset + 17 );
    TerrainData::TerrainStreamCache cache( f.Archive ); TerrainData::StreamBatch batch{ &cache, {}, { { { 0, 0 }, true, false, 0, true } } };
    TerrainData::TerrainStreamScheduler scheduler; TerrainData::StreamWorldLimits limits;
    for( uint64_t frame = 0; frame < 30; ++frame ) { cache.WaitForRequest(); scheduler.Tick( frame, { &batch, 1 }, limits ); }
    CHECK( cache.GetStats().Failed == 1 ); CHECK( cache.GetStats().CollisionReady ); CHECK_FALSE( cache.GetStats().HorizonReady ); CHECK( cache.GetData()->Patches.empty() );
    CHECK( cache.GetData()->Heights->Query( 3, 3 ).Status == TerrainData::QueryStatus::Ready );
    PageFixture( f.File, f.Field, f.Archive ); TerrainData::TerrainStreamCache restarted( f.Archive ); batch.Cache = &restarted; batch.Demand = { { { 0, 0 }, false, false, 0, true } };
    scheduler.Tick( 1, { &batch, 1 }, limits ); REQUIRE( restarted.GetStats().Loading ); restarted.InvalidatePending();
    for( uint64_t frame = 2; frame < 25; ++frame ) { restarted.WaitForRequest(); scheduler.Tick( frame, { &batch, 1 }, limits ); }
    CHECK( restarted.GetStats().Cancelled >= 1 ); CHECK( restarted.GetStats().CoverageLoaded == 1 ); CHECK( restarted.GetStats().HorizonReady ); CHECK( restarted.GetStats().Loaded == 0 );
}

TEST_CASE( "Terrain paging: datasets beyond the resident patch limit open and draw only a bounded window" )
{
    StreamFixture f; TerrainData::Settings s; s.TileQuads = 8; s.PatchQuads = 2; s.MinTile = { -32, -32 }; s.TilesX = s.TilesZ = 65;
    f.Field = TerrainData::Heightfield( s ); std::string error;
    for( int x = -32; x <= 32; ++x ) for( int z = -32; z <= 32; ++z ) REQUIRE( f.Field.CreateFlatTile( { x, z }, 3, error ) );
    CHECK_FALSE( TerrainData::CookStreamArchive( f.File, f.Field, 123, 1, error ) );
    PageFixture( f.File, f.Field, f.Archive ); CHECK( f.Archive->Coverage->Patches.empty() ); CHECK( f.Archive->Blocks.size() == 4225 );
    TerrainData::TerrainStreamCache cache( f.Archive ); TerrainData::StreamBatch batch{ &cache, {}, { { { -1, -1 }, false, false, 0, true }, { { 0, 0 }, false, false, 1, true } } };
    TerrainData::TerrainStreamScheduler scheduler; TerrainData::StreamWorldLimits limits; limits.CpuBytes = 2 * 1024 * 1024; limits.GpuBytes = 1024 * 1024;
    for( uint64_t frame = 0; frame < 20; ++frame )
    {
        cache.WaitForRequest(); scheduler.Tick( frame, { &batch, 1 }, limits );
        CHECK( scheduler.GetStats().CpuReserved <= limits.CpuBytes ); CHECK( scheduler.GetStats().GpuReserved <= limits.GpuBytes );
    }
    CHECK( cache.GetData()->Patches.size() == 32 ); CHECK( cache.GetStats().CoverageLoaded == 2 ); CHECK( cache.GetStats().Loaded == 0 );
}

TEST_CASE( "Terrain paging: tight CPU grants remain stable after the coverage window publishes" )
{
    StreamFixture f; PageFixture( f.File, f.Field, f.Archive ); TerrainData::TerrainStreamCache cache( f.Archive );
    const auto cost = TerrainData::TerrainStreamCache::CoverageReservation( f.Field.GetSettings(), 1 );
    TerrainData::StreamWorldLimits limits; limits.CpuBytes = cache.GetStats().CpuReserved + 4096 + 4 * ( cost.CpuBytes + 4 * 256 + 512 );
    TerrainData::StreamBatch batch{ &cache };
    for( int x = -1; x <= 0; ++x ) for( int z = -1; z <= 0; ++z ) batch.Demand.push_back( { { x, z }, false, false, 0, true } );
    TerrainData::TerrainStreamScheduler scheduler;
    for( uint64_t frame = 0; frame < 50; ++frame )
    {
        cache.WaitForRequest(); scheduler.Tick( frame, { &batch, 1 }, limits ); CHECK( scheduler.GetStats().CpuReserved <= limits.CpuBytes );
        if( frame > 10 ) CHECK( cache.GetData()->CoverageTiles.size() == 4 );
    }
    CHECK( cache.GetStats().CoverageLoaded == 4 ); CHECK( cache.GetStats().CoverageEvicted == 0 );
}

namespace
{
    std::shared_ptr<const TerrainData::Surface> PaintFixture( StreamFixture& f )
    {
        auto surface = std::make_shared<TerrainData::Surface>();
        surface->BaseWeights = { 10, 20, 30, 195 };
        for( auto& layer : surface->Layers )
        {
            layer.BaseColorMap = "Assets/Textures/Terrain/Base.tga"; layer.NormalMap = "Assets/Textures/Terrain/Normal.tga";
            layer.OrmMap = "Assets/Textures/Terrain/ORM.tga"; layer.TileSize = 2.5f; layer.NormalStrength = 0.4f;
            layer.Metallic = 0.3f; layer.FlipNormalY = true;
        }
        for( int z = -8; z <= 16; ++z ) for( int x = -8; x <= 16; ++x )
            surface->Set( { x, z }, TerrainData::NormalizeWeights( { float( x + 9 ), float( z + 9 ), float( ( x * x + z * z ) % 23 ), 5 } ) );
        std::string error; REQUIRE( f.Field.SetSurface( surface, error ) ); PageFixture( f.File, f.Field, f.Archive ); return surface;
    }


    std::vector<uint8_t> StreamBytes( const std::filesystem::path& path )
    {
        std::ifstream file( path, std::ios::binary ); return { std::istreambuf_iterator<char>( file ), std::istreambuf_iterator<char>() };
    }


    uint64_t StreamHash( std::span<const uint8_t> bytes )
    {
        uint64_t hash = 14695981039346656037ull; for( auto b : bytes ) hash = ( hash ^ b ) * 1099511628211ull; return hash;
    }


    void StreamPut64( std::vector<uint8_t>& bytes, size_t offset, uint64_t value )
    {
        for( int i = 0; i < 8; ++i ) bytes.at( offset + i ) = static_cast<uint8_t>( value >> ( i * 8 ) );
    }


    void WriteStreamBytes( const std::filesystem::path& path, const std::vector<uint8_t>& bytes )
    {
        std::ofstream file( path, std::ios::binary | std::ios::trunc ); file.write( reinterpret_cast<const char*>( bytes.data() ), bytes.size() );
    }
}

TEST_CASE( "Terrain streamed surfaces: metadata holds only the palette and page weights preserve canonical borders at every LOD" )
{
    StreamFixture f; const auto surface = PaintFixture( f ); std::string error;
    REQUIRE( f.Archive->Header[3] == '4' ); CHECK( TerrainData::IsStreamArchive( f.Archive->Header ) );
    REQUIRE( f.Archive->Coverage->Heights->GetSurface() );
    CHECK( f.Archive->Coverage->Heights->GetSurface()->Layers == surface->Layers );
    CHECK( f.Archive->Coverage->Heights->GetSurface()->BaseWeights == surface->BaseWeights );
    CHECK( f.Archive->Coverage->Heights->GetSurface()->Samples.empty() );
    CHECK( f.Archive->Coverage->Patches.empty() ); CHECK( f.Archive->Coverage->Heights->GetTiles().empty() );
    for( uint8_t level : { 0, 1, 2 } )
    {
        REQUIRE( TerrainData::CookPagedStreamArchive( f.File, f.Field, 123, level, error ) );
        REQUIRE( TerrainData::OpenStreamArchive( f.File, f.Archive, error ) );
        for( const auto& block : f.Archive->Blocks )
        {
            std::shared_ptr<const TerrainData::StreamCoverageTile> page;
            REQUIRE( TerrainData::ReadStreamCoverage( *f.Archive, block.Coordinate, page, error ) );
            for( const auto& patch : page->Patches )
            {
                CHECK( patch->BaseLod == level ); CHECK( patch->Mesh.Vertices.size() == size_t( ( 4 >> level ) + 1 ) * ( ( 4 >> level ) + 1 ) );
                CHECK( patch->Weights == TerrainData::BuildSurfacePatch( *surface, patch->Mesh.SampleOrigin, 4 ) );
            }
            std::shared_ptr<const TerrainData::StreamTile> tile;
            REQUIRE( TerrainData::ReadStreamTile( *f.Archive, block.Coordinate, tile, error ) );
            CHECK( tile->Heights == f.Field.FindTile( block.Coordinate )->Heights );
            for( const auto& patch : tile->Patches ) CHECK( patch->Weights.empty() );
        }
    }
}

TEST_CASE( "Terrain streamed surfaces: a damaged weight page preserves other pages and canonical collision" )
{
    StreamFixture f; PaintFixture( f ); std::string error;
    std::shared_ptr<const TerrainData::StreamCoverageTile> page;
    REQUIRE( TerrainData::ReadStreamCoverage( *f.Archive, { -1, -1 }, page, error ) ); const auto before = page;
    Flip( f.File, f.Archive->Blocks[4].CoverageOffset + f.Archive->Blocks[4].CoverageSize - 1 );
    REQUIRE( TerrainData::OpenStreamArchive( f.File, f.Archive, error ) ); // Metadata-only open never reads weight pages.
    CHECK_FALSE( TerrainData::ReadStreamCoverage( *f.Archive, { 0, 0 }, page, error ) ); CHECK( page == before );
    REQUIRE( TerrainData::ReadStreamCoverage( *f.Archive, { 1, 1 }, page, error ) );
    std::shared_ptr<const TerrainData::StreamTile> tile;
    REQUIRE( TerrainData::ReadStreamTile( *f.Archive, { 0, 0 }, tile, error ) ); REQUIRE( tile->Collision );
    TerrainData::TerrainStreamCache cache( f.Archive ); uint64_t frame = 0; TerrainData::StreamLimits limits;
    const std::vector<TerrainData::TileDemand> demand{ { { 0, 0 }, true, false, 0, true } }; Drain( cache, demand, limits, frame );
    CHECK( cache.GetStats().Failed == 1 ); CHECK( cache.GetStats().CollisionReady ); CHECK_FALSE( cache.GetStats().HorizonReady );
    CHECK( cache.GetData()->Heights->Query( 2, 2 ).Status == TerrainData::QueryStatus::Ready );
    const auto archive = f.Archive;
    Flip( f.File, 104 + f.Archive->Blocks.size() * 64 + 24 ); // Bounded palette follows the directory.
    CHECK_FALSE( TerrainData::OpenStreamArchive( f.File, f.Archive, error ) ); CHECK( f.Archive == archive );
}

TEST_CASE( "Terrain streamed surfaces: semantic weight validation survives recomputed checksums" )
{
    StreamFixture f; PaintFixture( f ); auto bytes = StreamBytes( f.File ); const auto block = f.Archive->Blocks[4];
    const size_t firstWeight = static_cast<size_t>( block.CoverageOffset ) + 8 + 2 * 4 + 9 * 16;
    std::fill_n( bytes.begin() + firstWeight, 4, 0 );
    StreamPut64( bytes, 104 + 4 * 64 + 56, StreamHash( std::span( bytes ).subspan( block.CoverageOffset, block.CoverageSize ) ) );
    StreamPut64( bytes, 88, StreamHash( std::span( bytes ).subspan( 104, f.Archive->Blocks.size() * 64 ) ) );
    StreamPut64( bytes, 96, StreamHash( std::span( bytes ).first( 96 ) ) ); WriteStreamBytes( f.File, bytes );
    std::string error; REQUIRE( TerrainData::OpenStreamArchive( f.File, f.Archive, error ) );
    std::shared_ptr<const TerrainData::StreamCoverageTile> page;
    CHECK_FALSE( TerrainData::ReadStreamCoverage( *f.Archive, { 0, 0 }, page, error ) ); CHECK( error.find( "normalized" ) != std::string::npos );
    std::shared_ptr<const TerrainData::StreamTile> tile; REQUIRE( TerrainData::ReadStreamTile( *f.Archive, { 0, 0 }, tile, error ) );
    StreamPut64( bytes, 72, 32793 ); StreamPut64( bytes, 96, StreamHash( std::span( bytes ).first( 96 ) ) ); WriteStreamBytes( f.File, bytes );
    const auto before = f.Archive; CHECK_FALSE( TerrainData::OpenStreamArchive( f.File, f.Archive, error ) ); CHECK( f.Archive == before );
}

TEST_CASE( "Terrain streamed surfaces: palette and paint changes invalidate generations and failed cooks preserve destinations" )
{
    StreamFixture f; auto surface = PaintFixture( f ); auto previous = f.Archive; std::string error;
    auto changed = std::make_shared<TerrainData::Surface>( *surface ); changed->Layers[1].TileSize = 37;
    REQUIRE( f.Field.SetSurface( changed, error ) ); PageFixture( f.File, f.Field, f.Archive );
    CHECK( previous->Revision != f.Archive->Revision ); CHECK( previous->Coverage->StreamRevision != f.Archive->Coverage->StreamRevision );
    std::shared_ptr<const TerrainData::StreamCoverageTile> page;
    CHECK_FALSE( TerrainData::ReadStreamCoverage( *previous, { 0, 0 }, page, error ) ); previous = f.Archive;
    changed = std::make_shared<TerrainData::Surface>( *changed ); changed->Set( { 0, 0 }, { 0, 0, 255, 0 } );
    REQUIRE( f.Field.SetSurface( changed, error ) ); PageFixture( f.File, f.Field, f.Archive ); CHECK( previous->Revision != f.Archive->Revision );
    std::shared_ptr<const TerrainData::StreamTile> tile; CHECK_FALSE( TerrainData::ReadStreamTile( *previous, { 0, 0 }, tile, error ) );
    auto before = StreamBytes( f.File ); CHECK_FALSE( TerrainData::CookPagedStreamArchive( f.File, f.Field, 123, 8, error ) ); CHECK( StreamBytes( f.File ) == before );
    CHECK_FALSE( TerrainData::CookStreamArchive( f.File, f.Field, 123, 1, error ) ); CHECK( StreamBytes( f.File ) == before );
}

TEST_CASE( "Terrain streamed surfaces: shared budgets charge full-resolution weights and retain draw pins across travel" )
{
    StreamFixture f; const auto surface = PaintFixture( f ); TerrainData::TerrainStreamCache cache( f.Archive );
    const auto plain = TerrainData::TerrainStreamCache::CoverageReservation( f.Field.GetSettings(), 1 );
    const auto painted = TerrainData::TerrainStreamCache::CoverageReservation( f.Field.GetSettings(), 1, true );
    CHECK( painted.CpuBytes > plain.CpuBytes ); CHECK( painted.GpuBytes > plain.GpuBytes );
    TerrainData::StreamWorldLimits limits; limits.CpuBytes = limits.GpuBytes = limits.CollisionBytes = 1024 * 1024;
    TerrainData::StreamBatch batch{ &cache, {}, { { { -1, -1 }, true, true, 0, true } } }; batch.Limits.HoldFrames = 0;
    TerrainData::TerrainStreamScheduler scheduler; uint64_t frame = 0;
    const auto step = [&]() { cache.WaitForRequest(); scheduler.Tick( ++frame, { &batch, 1 }, limits ); CHECK( scheduler.GetStats().CpuReserved <= limits.CpuBytes ); CHECK( scheduler.GetStats().GpuReserved <= limits.GpuBytes ); };
    for( int n = 0; n < 20; ++n ) step();
    REQUIRE( cache.GetStats().HorizonReady ); REQUIRE( cache.GetStats().Detail == 1 );
    auto data = cache.GetData(); REQUIRE( data->Heights->GetSurface() ); CHECK( data->Heights->GetSurface()->Layers == surface->Layers );
    auto pin = data->CoveragePatches[0]; CHECK( pin->Weights == TerrainData::BuildSurfacePatch( *surface, pin->Mesh.SampleOrigin, 4 ) ); data.reset();
    batch.Demand = { { { 1, 1 }, false, false, 0, true } };
    for( int n = 0; n < 20; ++n ) step();
    CHECK( cache.GetStats().CoverageRetiring == 1 ); CHECK( cache.GetStats().CoverageResident == 0 );
    CHECK_FALSE( cache.GetStats().HorizonReady ); CHECK( cache.GetStats().GpuReserved >= painted.GpuBytes );
    pin.reset(); for( int n = 0; n < 20; ++n ) step();
    CHECK( cache.GetStats().CoverageRetiring == 0 ); CHECK( cache.GetStats().CoverageResident == 1 ); CHECK( cache.GetStats().HorizonReady );
    for( const auto& patch : cache.GetData()->CoveragePatches ) CHECK( patch->Weights == TerrainData::BuildSurfacePatch( *surface, patch->Mesh.SampleOrigin, 4 ) );
    batch.Demand.clear(); for( int n = 0; n < 10; ++n ) step();
    CHECK( cache.GetStats().CoverageRetiring == 0 ); CHECK( cache.GetStats().CoverageResident == 0 );
}


TEST_CASE( "Terrain prefetch: optional canonical pages promote to required collision without a second read" )
{
    StreamFixture fixture;
    SUBCASE( "resident coverage" ) {}
    SUBCASE( "paged coverage" ) { PageFixture( fixture.File, fixture.Field, fixture.Archive ); }
    TerrainData::TerrainStreamCache cache( fixture.Archive ); TerrainData::StreamBatch batch{ &cache, {}, { { { 1, 1 }, false, false, 0, false, true } } };
    TerrainData::StreamWorldLimits limits; limits.CollisionBytes = 0; TerrainData::TerrainStreamScheduler scheduler;
    const auto gpu = cache.GetStats().GpuReserved; uint64_t frame = 0;
    const auto step = [&]() { cache.WaitForRequest(); scheduler.Tick( ++frame, std::span( &batch, 1 ), limits ); };
    for( int n = 0; n < 20; ++n ) step();
    REQUIRE( cache.GetStats().Loaded == 1 ); CHECK( cache.GetStats().PrefetchResident == 1 ); CHECK( cache.GetStats().PrefetchPending == 0 );
    CHECK( cache.GetStats().CollisionReady ); CHECK( scheduler.GetStats().CollisionReady ); CHECK( cache.GetStats().Detail == 0 );
    CHECK( cache.GetStats().GpuReserved == gpu ); CHECK( cache.GetStats().CollisionReserved == 0 ); CHECK( cache.GetData()->CollisionTiles.empty() );
    CHECK( cache.GetStats().CoverageLoaded == 0 );
    const auto actual = cache.GetData()->Heights->Query( 12.25, 12.5 ), expected = fixture.Field.Query( 12.25, 12.5 );
    REQUIRE( actual.Status == TerrainData::QueryStatus::Ready ); CHECK( actual.Height == expected.Height ); CHECK( actual.Normal.InternalVector == expected.Normal.InternalVector );
    batch.Demand[0].Collision = true; step(); CHECK_FALSE( cache.GetStats().CollisionReady );
    limits.CollisionBytes = TerrainData::TerrainStreamCache::TileReservation( fixture.Field.GetSettings() ).CollisionBytes;
    for( int n = 0; n < 5; ++n ) step();
    CHECK( cache.GetStats().CollisionReady ); CHECK( cache.GetStats().Loaded == 1 ); CHECK( cache.GetStats().PrefetchResident == 0 ); REQUIRE( cache.GetData()->CollisionTiles.size() == 1 );
}


TEST_CASE( "Terrain prefetch: world budgets admit another asset's required ground before optional look-ahead" )
{
    StreamFixture a, b; TerrainData::TerrainStreamCache first( a.Archive ), second( b.Archive );
    const auto cost = TerrainData::TerrainStreamCache::TileReservation( a.Field.GetSettings() );
    TerrainData::StreamWorldLimits limits;
    limits.CpuBytes = first.GetStats().CpuReserved + second.GetStats().CpuReserved + 2 * ( a.Archive->Coverage->Patches.size() * 128 + 4096 ) + cost.CpuBytes + a.Field.GetSettings().SamplesPerTile() * 16 + 1024;
    limits.CollisionBytes = cost.CollisionBytes; limits.MaxLoads = 1;
    TerrainData::StreamBatch batches[] = { { &first, {}, { { { -1, -1 }, false, false, 0, false, true } } }, { &second, {}, { { { 1, 1 }, true, false, 10000 } } } };
    TerrainData::TerrainStreamScheduler scheduler;
    for( uint64_t frame = 1; frame <= 30; ++frame )
    {
        first.WaitForRequest(); second.WaitForRequest(); scheduler.Tick( frame, batches, limits );
        CHECK( scheduler.GetStats().CpuReserved <= limits.CpuBytes ); CHECK( scheduler.GetStats().CollisionReserved <= limits.CollisionBytes ); CHECK( scheduler.GetStats().Loading <= 1 );
    }
    CHECK( first.GetStats().Loaded == 0 ); CHECK( first.GetStats().CollisionReady ); CHECK( second.GetStats().Loaded == 1 );
    CHECK( scheduler.GetStats().CollisionReady ); CHECK( scheduler.GetStats().Deferred == 1 );
}


TEST_CASE( "Terrain prefetch: corrupt optional pages never gate physics or retry until reload" )
{
    StreamFixture fixture;
    SUBCASE( "resident coverage" ) {}
    SUBCASE( "paged coverage" ) { PageFixture( fixture.File, fixture.Field, fixture.Archive ); }
    Flip( fixture.File, fixture.Archive->Blocks[fixture.Archive->TileIndex( { 1, 1 } )].Offset );
    TerrainData::TerrainStreamCache cache( fixture.Archive ); TerrainData::StreamLimits limits; uint64_t frame = 0;
    TerrainData::TileDemand request{ { 1, 1 }, false, false, 0, false, true };
    Drain( cache, std::span( &request, 1 ), limits, frame );
    REQUIRE( cache.GetStats().Failed == 1 ); CHECK( cache.GetStats().CollisionReady ); CHECK( cache.GetStats().PrefetchPending == 0 ); CHECK_FALSE( cache.GetStats().Error.empty() );
    for( int n = 0; n < 10; ++n ) cache.Tick( ++frame, std::span( &request, 1 ), limits );
    CHECK( cache.GetStats().Failed == 1 ); CHECK_FALSE( cache.GetStats().Loading );
    request.Collision = true; cache.Tick( ++frame, std::span( &request, 1 ), limits ); CHECK_FALSE( cache.GetStats().CollisionReady );
    Flip( fixture.File, fixture.Archive->Blocks[fixture.Archive->TileIndex( { 1, 1 } )].Offset );
    TerrainData::TerrainStreamCache reloaded( fixture.Archive ); Drain( reloaded, std::span( &request, 1 ), limits, frame ); CHECK( reloaded.GetStats().CollisionReady );
}


TEST_CASE( "Terrain prefetch: cancelled look-ahead cannot publish after a teleport and pins retain their charge" )
{
    StreamFixture fixture; PageFixture( fixture.File, fixture.Field, fixture.Archive );
    std::promise<void> entered, release; auto enteredFuture = entered.get_future(); auto gate = release.get_future().share();
    TerrainData::TerrainStreamCache cache( fixture.Archive, [&]( const auto& archive, auto coord, auto& out, auto& error ) {
        entered.set_value(); gate.wait(); return TerrainData::ReadStreamTile( archive, coord, out, error );
    } );
    TerrainData::StreamLimits limits; limits.HoldFrames = 0; uint64_t frame = 0;
    TerrainData::TileDemand request{ { 1, 1 }, false, false, 0, false, true };
    cache.Tick( ++frame, std::span( &request, 1 ), limits ); enteredFuture.wait();
    cache.Tick( ++frame, {}, limits ); release.set_value(); cache.WaitForRequest(); Drain( cache, {}, limits, frame );
    CHECK( cache.GetStats().Cancelled == 1 ); CHECK( cache.GetStats().Loaded == 0 ); CHECK( cache.GetStats().PrefetchResident == 0 );
    CHECK( cache.GetData()->Heights->Query( 12, 12 ).Status == TerrainData::QueryStatus::Unavailable );
    TerrainData::TerrainStreamCache warm( fixture.Archive ); Drain( warm, std::span( &request, 1 ), limits, frame ); auto pin = warm.GetData();
    const auto reserved = warm.GetStats().CpuReserved; warm.Tick( ++frame, {}, limits );
    CHECK( warm.GetStats().Retiring == 1 ); CHECK( warm.GetStats().CpuReserved >= reserved ); CHECK( warm.GetStats().PrefetchResident == 0 );
    pin.reset(); Drain( warm, {}, limits, frame ); CHECK( warm.GetStats().Retiring == 0 ); CHECK( warm.GetStats().Evicted == 1 );
}
