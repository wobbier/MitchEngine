#include <doctest/doctest.h>
#include "Terrain/TerrainPreview.h"
#include "Terrain/TerrainEditing.h"
#include "Terrain/TerrainSurface.h"
#include <chrono>
#include <cstdio>
#include <future>
#include <stdexcept>
#include <thread>

namespace
{
    TerrainData::Heightfield PreviewField( float height = 0 )
    {
        TerrainData::Settings s; s.TileQuads = 8; s.PatchQuads = 4; s.TilesX = s.TilesZ = 2; s.MinTile = { -1, -1 };
        TerrainData::Heightfield field( s ); std::string error;
        for( int x = -1; x <= 0; ++x ) for( int z = -1; z <= 0; ++z ) REQUIRE( field.CreateFlatTile( { x, z }, height, error ) );
        return field;
    }


    std::shared_ptr<const TerrainData::ResidentTerrain> InitialPreview( const TerrainData::Heightfield& field )
    {
        std::shared_ptr<const TerrainData::ResidentTerrain> data; std::string error;
        REQUIRE_MESSAGE( TerrainData::BuildResidentTerrain( field, 123, data, error ), error ); return data;
    }


    struct ReleaseWorker
    {
        std::promise<void>& Gate;
        bool Done = false;
        void Release() { if( !Done ) { Gate.set_value(); Done = true; } }
        ~ReleaseWorker() { Release(); }
    };
}

TEST_CASE( "Terrain preview: edits coalesce into one worker and only the latest immutable revision publishes" )
{
    auto field = PreviewField(); auto preview = InitialPreview( field ); const auto original = preview; std::string error;
    std::promise<void> entered, release; auto gate = release.get_future().share(); const auto owner = std::this_thread::get_id();
    std::atomic_uint calls = 0; std::atomic_bool otherThread = false; float captured = 0;
    TerrainData::TerrainPreview worker( [&]( const auto& source, auto guid, const auto& previous, auto& out, auto& failure, auto* cancel ) {
        otherThread = std::this_thread::get_id() != owner;
        if( ++calls == 1 ) { entered.set_value(); gate.wait(); source.GetSample( { -6, -6 }, captured ); }
        return TerrainData::UpdateResidentTerrain( source, guid, previous, out, failure, cancel );
    } );
    ReleaseWorker cleanup{ release };
    REQUIRE( field.SetSample( { -6, -6 }, 1, error ) ); REQUIRE( worker.Request( field, 123, preview, error ) );
    REQUIRE( entered.get_future().wait_for( std::chrono::seconds( 5 ) ) == std::future_status::ready );
    CHECK( worker.IsBusy() ); CHECK( preview == original );
    for( int i = 2; i <= 30; ++i ) { REQUIRE( field.SetSample( { -6, -6 }, float( i ), error ) ); REQUIRE( worker.Request( field, 123, preview, error ) ); }
    CHECK( calls == 1 ); CHECK( worker.GetStatistics().Started == 1 ); CHECK( preview == original );
    cleanup.Release(); REQUIRE_MESSAGE( worker.Wait( field, 123, preview, error ), error );
    CHECK( otherThread ); CHECK( captured == 1 ); CHECK( preview != original ); CHECK( preview->Heights->Query( -6, -6 ).Height == 30 );
    CHECK( original->Heights->Query( -6, -6 ).Height == 0 ); CHECK( preview->BuiltPatches == 1 ); CHECK( preview->BuiltCollisionTiles == 1 );
    CHECK( worker.GetStatistics().Started == 2 ); CHECK( worker.GetStatistics().Published == 1 ); CHECK( worker.GetStatistics().Discarded == 1 ); CHECK_FALSE( worker.IsBusy() );
}


TEST_CASE( "Terrain preview: obsolete failures cannot replace newer data or its error state" )
{
    auto field = PreviewField(); auto preview = InitialPreview( field ); std::string error;
    std::promise<void> entered, release; auto gate = release.get_future().share(); std::atomic_uint calls = 0;
    TerrainData::TerrainPreview worker( [&]( const auto& source, auto guid, const auto& previous, auto& out, auto& failure, auto* cancel ) {
        if( ++calls == 1 ) { entered.set_value(); gate.wait(); throw std::runtime_error( "obsolete failure" ); }
        return TerrainData::UpdateResidentTerrain( source, guid, previous, out, failure, cancel );
    } );
    ReleaseWorker cleanup{ release };
    REQUIRE( field.SetSample( { -6, -6 }, 1, error ) ); REQUIRE( worker.Request( field, 123, preview, error ) );
    REQUIRE( entered.get_future().wait_for( std::chrono::seconds( 5 ) ) == std::future_status::ready );
    REQUIRE( field.SetSample( { -6, -6 }, 2, error ) ); REQUIRE( worker.Request( field, 123, preview, error ) ); cleanup.Release();
    REQUIRE_MESSAGE( worker.Wait( field, 123, preview, error ), error ); CHECK( error.empty() ); CHECK( preview->Heights->Query( -6, -6 ).Height == 2 );
}


TEST_CASE( "Terrain preview: source reload invalidates in-flight work even when GUID and revision counters repeat" )
{
    auto field = PreviewField(); auto preview = InitialPreview( field ); std::string error;
    std::promise<void> entered, release; auto gate = release.get_future().share();
    TerrainData::TerrainPreview worker( [&]( const auto& source, auto guid, const auto& previous, auto& out, auto& failure, auto* ) {
        entered.set_value(); gate.wait(); return TerrainData::UpdateResidentTerrain( source, guid, previous, out, failure );
    } );
    ReleaseWorker cleanup{ release };
    REQUIRE( field.SetSample( { -6, -6 }, 1, error ) ); REQUIRE( worker.Request( field, 123, preview, error ) );
    REQUIRE( entered.get_future().wait_for( std::chrono::seconds( 5 ) ) == std::future_status::ready );
    auto replacement = PreviewField( 7 ); REQUIRE( replacement.SetSample( { -6, -6 }, 8, error ) ); CHECK( replacement.GetRevision() == field.GetRevision() );
    worker.Reset(); field = std::move( replacement ); preview = InitialPreview( field ); const auto loaded = preview;
    cleanup.Release(); REQUIRE( worker.Wait( field, 123, preview, error ) );
    CHECK( preview == loaded ); CHECK( preview->Heights->Query( -6, -6 ).Height == 8 ); CHECK( worker.GetStatistics().Published == 0 ); CHECK( worker.GetStatistics().Discarded == 1 );
}


TEST_CASE( "Terrain preview: failed current work preserves the last preview and requires a changed source or explicit retry" )
{
    auto field = PreviewField(); auto preview = InitialPreview( field ); const auto original = preview; std::string error; bool fail = true;
    TerrainData::TerrainPreview worker( [&]( const auto& source, auto guid, const auto& previous, auto& out, auto& failure, auto* cancel ) {
        if( fail ) throw std::runtime_error( "injected preview failure" );
        return TerrainData::UpdateResidentTerrain( source, guid, previous, out, failure, cancel );
    } );
    REQUIRE( field.SetSample( { 6, 6 }, 2, error ) ); CHECK_FALSE( worker.Wait( field, 123, preview, error ) );
    CHECK( error == "injected preview failure" ); CHECK( preview == original ); CHECK_FALSE( worker.IsBusy() );
    for( int i = 0; i < 10; ++i ) CHECK_FALSE( worker.Request( field, 123, preview, error ) );
    CHECK( worker.GetStatistics().Started == 1 ); fail = false; worker.Reset();
    REQUIRE( worker.Wait( field, 123, preview, error ) ); CHECK( error.empty() ); CHECK( preview->Heights->Query( 6, 6 ).Height == 2 ); CHECK( worker.GetStatistics().Started == 2 );
}


TEST_CASE( "Terrain preview: paint and saved dirty-rectangle cleanup preserve shared geometry through undo" )
{
    auto field = PreviewField(); auto preview = InitialPreview( field ); const auto original = preview; std::string error;
    TerrainData::TerrainPreview worker;
    auto surface = std::make_shared<TerrainData::Surface>(); surface->Set( { 0, 0 }, { 0, 255, 0, 0 } );
    REQUIRE( field.SetSurface( surface, error ) ); REQUIRE( worker.Request( field, 123, preview, error ) ); field.ClearDirty();
    REQUIRE( worker.Wait( field, 123, preview, error ) ); CHECK( preview->Heights->GetSurface() == surface ); CHECK( preview->Patches == original->Patches ); CHECK( preview->CollisionTiles == original->CollisionTiles );
    REQUIRE( field.SetSurface( nullptr, error ) ); REQUIRE( worker.Wait( field, 123, preview, error ) ); CHECK_FALSE( preview->Heights->GetSurface() ); CHECK( preview->Patches == original->Patches );
    REQUIRE( field.SetSample( { 0, 0 }, 4, error ) ); REQUIRE( worker.Request( field, 123, preview, error ) ); field.ClearDirty();
    REQUIRE( worker.Wait( field, 123, preview, error ) ); CHECK( preview->Heights->Query( 0, 0 ).Height == 4 ); CHECK( preview->BuiltCollisionTiles == 4 );
    REQUIRE( field.SetSample( { 0, 0 }, 0, error ) ); REQUIRE( worker.Wait( field, 123, preview, error ) ); CHECK( preview->Heights->Query( 0, 0 ).Height == 0 );
    const auto started = worker.GetStatistics().Started; REQUIRE( worker.Wait( field, 123, preview, error ) ); CHECK( worker.GetStatistics().Started == started );
}


TEST_CASE( "Terrain preview: destruction cancels and joins its worker and cancelled builds preserve output" )
{
    auto field = PreviewField(); auto preview = InitialPreview( field ); const auto original = preview; std::string error;
    std::atomic_bool cancel = true; CHECK_FALSE( TerrainData::UpdateResidentTerrain( field, 123, original, preview, error, &cancel ) ); CHECK( preview == original );
    std::promise<void> entered; std::atomic_bool exited = false;
    {
        TerrainData::TerrainPreview worker( [&]( const auto&, auto, const auto&, auto&, auto&, auto* token ) {
            entered.set_value(); while( !token->load( std::memory_order_relaxed ) ) std::this_thread::yield(); exited = true; return false;
        } );
        REQUIRE( field.SetSample( { 0, 0 }, 1, error ) ); REQUIRE( worker.Request( field, 123, preview, error ) );
        REQUIRE( entered.get_future().wait_for( std::chrono::seconds( 5 ) ) == std::future_status::ready ); CHECK_FALSE( exited );
    }
    CHECK( exited ); CHECK( preview == original );
}


TEST_CASE( "Terrain preview: resident authoring settings have one consistent checked sample limit" )
{
    TerrainData::Settings s; s.TileQuads = 32; s.PatchQuads = 16; s.TilesX = s.TilesZ = 31; std::string error;
    CHECK( TerrainData::ValidateAuthoringSettings( s, error ) ); s.TilesX = s.TilesZ = 32;
    CHECK_FALSE( TerrainData::ValidateAuthoringSettings( s, error ) ); CHECK( error.find( "one million" ) != std::string::npos );
    s.TilesX = s.TilesZ = 1; s.PatchQuads = 3; CHECK_FALSE( TerrainData::ValidateAuthoringSettings( s, error ) );
    s.TileQuads = s.PatchQuads = 1; s.TilesX = s.TilesZ = 512; CHECK( TerrainData::ValidateAuthoringSettings( s, error ) );
    s.TilesX = 513; CHECK_FALSE( TerrainData::ValidateAuthoringSettings( s, error ) );
}


TEST_CASE( "Terrain preview: a near-limit source prepares one changed patch and collision tile while sharing the rest" )
{
    TerrainData::Settings settings; settings.TileQuads = 256; settings.PatchQuads = 64; settings.MinTile = { -1, -2 }; settings.TilesX = 3; settings.TilesZ = 5;
    std::string error; REQUIRE( TerrainData::ValidateAuthoringSettings( settings, error ) );
    const uint64_t samples = uint64_t( settings.TilesX ) * settings.TilesZ * settings.SamplesPerTile(); CHECK( samples == 990735 );
    TerrainData::Heightfield field( settings );
    for( int x = -1; x < 2; ++x ) for( int z = -2; z < 3; ++z ) REQUIRE( field.CreateFlatTile( { x, z }, 0, error ) );
    auto preview = InitialPreview( field ); const auto original = preview; TerrainData::TerrainPreview worker;
    REQUIRE( field.SetSample( { -200, -400 }, 2, error ) );
    const auto started = std::chrono::steady_clock::now(); REQUIRE( worker.Request( field, 123, preview, error ) );
    const double capture = std::chrono::duration<double, std::milli>( std::chrono::steady_clock::now() - started ).count();
    REQUIRE_MESSAGE( worker.Wait( field, 123, preview, error ), error );
    CHECK( preview->BuiltPatches == 1 ); CHECK( preview->BuiltCollisionTiles == 1 ); CHECK( preview->Patches.size() == 240 );
    size_t shared = 0; for( size_t i = 0; i < preview->Patches.size(); ++i ) shared += preview->Patches[i] == original->Patches[i]; CHECK( shared == 239 );
    CHECK( preview->Heights->Query( -200, -400 ).Height == 2 ); CHECK( original->Heights->Query( -200, -400 ).Height == 0 );
    std::printf( "[terrain-preview-limit] samples=%llu capture-ms=%.3f worker-ms=%.3f shared-patches=%zu/240\n", static_cast<unsigned long long>( samples ), capture, worker.GetStatistics().LastMilliseconds, shared );
}
