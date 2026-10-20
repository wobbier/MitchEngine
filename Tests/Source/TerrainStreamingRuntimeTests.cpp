#include <doctest/doctest.h>
#include "Components/Graphics/Terrain.h"
#include "Components/Physics/CharacterController.h"
#include "Components/Physics/Rigidbody.h"
#include "Components/Physics/Colliders.h"
#include "Components/Transform.h"
#include "Components/Camera.h"
#include "Cores/TerrainCore.h"
#include "Cores/PhysicsCore.h"
#include "Core/UpdateContext.h"
#include "Engine/World.h"
#include "Engine/ProjectSettings.h"
#include "Terrain/TerrainStreamArchive.h"
#include "Terrain/TerrainStreamCache.h"
#include <filesystem>

namespace
{
    struct StreamScene
    {
        TerrainCore Terrains;
        PhysicsCore Physics;
        std::shared_ptr<World> GameWorld = std::make_shared<World>();
        struct Context : UpdateContext { using UpdateContext::BeginFrame; using UpdateContext::FixedDeltaTime; using UpdateContext::IsFixedStepActive; };
        Context Time;
        Path File;
        EntityHandle Ground, Player;
        StreamScene( bool paged = false )
        {
            static int sequence = 0; const auto path = std::filesystem::path( ".tmp/Tests/TerrainStreamingRuntime" ) / std::to_string( ++sequence ) / "Runtime.terrainc";
            TerrainData::Settings s; s.TileQuads = 16; s.PatchQuads = 8; s.MinTile = { -2, -2 }; s.TilesX = s.TilesZ = 5;
            TerrainData::Heightfield field( s ); std::string error;
            for( int x = -2; x <= 2; ++x ) for( int z = -2; z <= 2; ++z ) REQUIRE( field.CreateFlatTile( { x, z }, 3, error ) );
            REQUIRE( ( paged ? TerrainData::CookPagedStreamArchive( path, field, 345, 2, error ) : TerrainData::CookStreamArchive( path, field, 345, 2, error ) ) ); File = Path( path.generic_string() );
            GameWorld->IsLoading = false; GameWorld->AddCore( Terrains ); GameWorld->AddCore( Physics );
            Ground = GameWorld->CreateEntity( "Ground" ); Ground->AddComponent<Transform>(); auto& terrain = Ground->AddComponent<Terrain>(); terrain.Asset = File;
            terrain.CpuBudgetMiB = terrain.GpuBudgetMiB = terrain.CollisionBudgetMiB = 1; terrain.CollisionDistance = 1;
            Player = GameWorld->CreateEntity( "Player" ); Player->AddComponent<Transform>().SetPosition( Vector3( 0, 10, 0 ) ); Player->AddComponent<CharacterController>();
            GameWorld->Simulate(); Terrains.SyncNow( true );
            Time.FixedDeltaTime = 1.f / 60;
        }
        void Step()
        {
            Time.BeginFrame( 1.f / 60, 1.f / 60, 1, false ); GameWorld->Simulate(); Terrains.Update( Time );
            Time.IsFixedStepActive = true; Physics.FixedUpdate( Time ); Time.IsFixedStepActive = false; Physics.Update( Time );
        }
        void Drain()
        {
            for( int n = 0; n < 40; ++n ) { Ground->GetComponent<Terrain>().WaitForStreamingRequest(); Physics.WaitForTerrainCollision(); Step(); }
        }
    };
}

TEST_CASE( "Terrain streaming: collision budget pressure holds simulation until canonical ground is ready" )
{
    StreamScene scene; scene.GameWorld->Start(); scene.Drain();
    auto& terrain = scene.Ground->GetComponent<Terrain>();
    REQUIRE( terrain.GetStreamingStats() ); CHECK_FALSE( scene.Terrains.IsCollisionReady() );
    CHECK( scene.Player->GetComponent<Transform>().GetWorldPosition().y == doctest::Approx( 10 ) );
    CHECK_FALSE( terrain.GetStreamingStats()->Pressure.empty() );
    terrain.CollisionBudgetMiB = 2; scene.Drain();
    REQUIRE( scene.Terrains.IsCollisionReady() ); REQUIRE( scene.Physics.IsTerrainCollisionReady() );
    for( int n = 0; n < 120; ++n ) scene.Step();
    const float y = scene.Player->GetComponent<Transform>().GetWorldPosition().y;
    CHECK( y > 3.5f ); CHECK( y < 4.1f );
    RaycastHit hit; REQUIRE( scene.Physics.Raycast( Vector3( 2, 20, 2 ), Vector3( 0, -1, 0 ), 30, hit ) ); CHECK( hit.Entity == scene.Ground ); CHECK( hit.Position.y == doctest::Approx( 3 ) );
}

TEST_CASE( "Terrain streaming: teleport pauses before a new tile cook and actor collision ignores camera demand" )
{
    StreamScene scene; auto& terrain = scene.Ground->GetComponent<Terrain>(); terrain.CollisionBudgetMiB = 4;
    scene.GameWorld->Start(); scene.Drain(); REQUIRE( scene.Terrains.IsCollisionReady() );
    const auto loaded = terrain.GetStreamingStats()->Loaded;
    scene.Player->GetComponent<CharacterController>().Teleport( Vector3( 24, 20, 24 ), Quaternion() );
    scene.Terrains.SyncNow(); CHECK_FALSE( scene.Terrains.IsCollisionReady() ); scene.Step();
    CHECK( scene.Player->GetComponent<Transform>().GetWorldPosition().y == doctest::Approx( 20 ) );
    scene.Drain(); REQUIRE( scene.Terrains.IsCollisionReady() ); REQUIRE( scene.Physics.IsTerrainCollisionReady() );
    CHECK( terrain.GetStreamingStats()->Loaded > loaded );
    CHECK( terrain.GetStreamingStats()->Evicted >= 4 );
    CHECK( terrain.GetData()->Heights->Query( 24, 24 ).Status == TerrainData::QueryStatus::Ready );
    CHECK( terrain.GetStreamingStats()->CpuReserved <= uint64_t( terrain.CpuBudgetMiB ) * 1024 * 1024 );
}

TEST_CASE( "Terrain streaming: instances share decoded tiles and collision cooks through unload" )
{
    StreamScene scene; auto& first = scene.Ground->GetComponent<Terrain>(); first.CpuBudgetMiB = first.GpuBudgetMiB = first.CollisionBudgetMiB = 8;
    auto second = scene.GameWorld->CreateEntity( "Second" ); second->AddComponent<Transform>().SetPosition( Vector3( 1000, 0, 0 ) );
    auto& other = second->AddComponent<Terrain>(); other.Asset = scene.File; other.CpuBudgetMiB = other.GpuBudgetMiB = other.CollisionBudgetMiB = 8; other.CollisionDistance = 1;
    scene.GameWorld->Start(); scene.Drain();
    REQUIRE( first.GetData() == other.GetData() ); REQUIRE( first.GetStreamingStats() == other.GetStreamingStats() );
    REQUIRE( scene.Terrains.IsCollisionReady() ); CHECK( scene.Physics.GetTerrainCollisionBuildCount() == 4 );
    const auto loaded = first.GetStreamingStats()->Loaded;
    scene.Ground->SetActive( false ); scene.Player->GetComponent<CharacterController>().Teleport( Vector3( 1000, 10, 0 ), Quaternion() ); scene.Drain();
    CHECK( scene.Terrains.IsCollisionReady() ); CHECK( other.GetStreamingStats()->Loaded == loaded ); CHECK( scene.Physics.GetTerrainCollisionBuildCount() == 4 );
    second->SetActive( false ); scene.Player->SetActive( false ); scene.Step();
    CHECK( scene.Physics.GetTerrainCollisionMeshCount() == 0 );
}

TEST_CASE( "Terrain streaming: pending capsule movement waits before sweeping across unloaded ground" )
{
    StreamScene scene; auto& terrain = scene.Ground->GetComponent<Terrain>(); terrain.CpuBudgetMiB = terrain.GpuBudgetMiB = terrain.CollisionBudgetMiB = 2;
    scene.GameWorld->Start(); scene.Drain(); REQUIRE( scene.Terrains.IsCollisionReady() );
    auto& player = scene.Player->GetComponent<CharacterController>(); player.Move( Vector3( 16, 0, 0 ) ); scene.Terrains.SyncNow();
    CHECK_FALSE( scene.Terrains.IsCollisionReady() ); scene.Step(); CHECK( scene.Player->GetComponent<Transform>().GetWorldPosition().x == doctest::Approx( 0 ) );
    player.Move( Vector3( -16, 0, 0 ) ); scene.Drain(); CHECK( scene.Terrains.IsCollisionReady() );
}

TEST_CASE( "Terrain streaming: vertical falling velocity does not expand horizontal collision demand" )
{
    StreamScene scene; scene.Player->SetActive( false );
    auto actor = scene.GameWorld->CreateEntity( "Falling" ); actor->AddComponent<Transform>().SetPosition( Vector3( 24, 500, 24 ) ); actor->AddComponent<BoxCollider>(); auto& body = actor->AddComponent<Rigidbody>();
    scene.GameWorld->Simulate(); scene.Physics.SyncNow(); body.SetVelocity( Vector3( 0, -100, 0 ) ); scene.GameWorld->Start(); scene.Drain();
    REQUIRE( scene.Terrains.IsCollisionReady() ); auto& terrain = scene.Ground->GetComponent<Terrain>();
    REQUIRE( terrain.GetData() ); CHECK( terrain.GetData()->CollisionTiles.size() == 1 ); CHECK( terrain.GetStreamingStats()->CollisionReserved <= 1024 * 1024 );
}

TEST_CASE( "Terrain streaming: shared world limits hold two assets until collision capacity is raised" )
{
    auto& settings = ProjectSettings::Get();
    struct Restore
    {
        ProjectSettings& Settings;
        int Cpu, Gpu, Collision, Loads;
        ~Restore() { Settings.TerrainCpuBudgetMiB = Cpu; Settings.TerrainGpuBudgetMiB = Gpu; Settings.TerrainCollisionBudgetMiB = Collision; Settings.TerrainMaxLoads = Loads; }
    } restore{ settings, settings.TerrainCpuBudgetMiB, settings.TerrainGpuBudgetMiB, settings.TerrainCollisionBudgetMiB, settings.TerrainMaxLoads };
    settings.TerrainCpuBudgetMiB = settings.TerrainGpuBudgetMiB = 4; settings.TerrainCollisionBudgetMiB = 2; settings.TerrainMaxLoads = 1;
    StreamScene scene;
    const auto path = std::filesystem::path( scene.File.FullPath ).parent_path() / "Second.terrainc";
    std::filesystem::copy_file( scene.File.FullPath, path, std::filesystem::copy_options::overwrite_existing );
    auto second = scene.GameWorld->CreateEntity( "Second ground" ); second->AddComponent<Transform>().SetPosition( Vector3( 1000, 0, 0 ) );
    auto& terrain = second->AddComponent<Terrain>(); terrain.Asset = Path( path.generic_string() ); terrain.CpuBudgetMiB = terrain.GpuBudgetMiB = terrain.CollisionBudgetMiB = 4; terrain.CollisionDistance = 1;
    scene.Ground->GetComponent<Terrain>().CpuBudgetMiB = scene.Ground->GetComponent<Terrain>().CollisionBudgetMiB = 4;
    auto player = scene.GameWorld->CreateEntity( "Second player" ); player->AddComponent<Transform>().SetPosition( Vector3( 1000, 10, 0 ) ); player->AddComponent<CharacterController>();
    scene.GameWorld->Start();
    const auto drain = [&]() {
        for( int n = 0; n < 60; ++n )
        {
            scene.Ground->GetComponent<Terrain>().WaitForStreamingRequest(); terrain.WaitForStreamingRequest(); scene.Physics.WaitForTerrainCollision(); scene.Step();
            const auto& stats = scene.Terrains.GetStreamingStats(); CHECK( stats.Loading <= 1 ); CHECK( stats.CpuReserved <= 4ull * 1024 * 1024 ); CHECK( stats.CollisionReserved <= uint64_t( settings.TerrainCollisionBudgetMiB ) * 1024 * 1024 );
        }
    };
    drain(); REQUIRE( scene.Terrains.GetStreamingStats().ActiveAssets == 2 ); CHECK_FALSE( scene.Terrains.IsCollisionReady() );
    CHECK( scene.Player->GetComponent<Transform>().GetWorldPosition().y == doctest::Approx( 10 ) ); CHECK( player->GetComponent<Transform>().GetWorldPosition().y == doctest::Approx( 10 ) );
    settings.TerrainCollisionBudgetMiB = 4; drain(); REQUIRE( scene.Terrains.IsCollisionReady() ); REQUIRE( scene.Physics.IsTerrainCollisionReady() );
    RaycastHit hit; REQUIRE( scene.Physics.Raycast( Vector3( 1002, 20, 2 ), Vector3( 0, -1, 0 ), 30, hit ) ); CHECK( hit.Entity == second ); CHECK( hit.Position.y == doctest::Approx( 3 ) );
    second->SetActive( false ); player->SetActive( false ); drain(); scene.Step();
    CHECK( scene.Terrains.GetStreamingStats().ActiveAssets == 1 ); CHECK( scene.Terrains.GetStreamingStats().RetiringAssets == 0 );
}

TEST_CASE( "Terrain paging: physics and canonical queries stay independent of the moving visual horizon" )
{
    StreamScene scene( true ); auto& terrain = scene.Ground->GetComponent<Terrain>(); terrain.CollisionBudgetMiB = 2; terrain.StreamDetail = false; terrain.CoverageDistance = 24;
    auto camera = scene.GameWorld->CreateEntity( "Coverage camera" ); camera->AddComponent<Transform>().SetPosition( Vector3( 24, 20, 24 ) ); camera->AddComponent<Camera>().OutputSize = Vector2( 1280, 720 );
    scene.GameWorld->Start(); scene.Drain(); REQUIRE( scene.Terrains.IsCollisionReady() ); REQUIRE( terrain.GetStreamingStats()->HorizonReady );
    CHECK( terrain.GetData()->Patches.size() == 36 ); CHECK( terrain.GetData()->Heights->Query( 40, 40 ).Status == TerrainData::QueryStatus::Unavailable );
    scene.Player->GetComponent<CharacterController>().Teleport( Vector3( 24, 20, 24 ), Quaternion() );
    camera->GetComponent<Transform>().SetPosition( Vector3( -24, 20, -24 ) ); scene.Terrains.SyncNow(); CHECK_FALSE( scene.Terrains.IsCollisionReady() );
    for( int n = 0; n < 2; ++n ) scene.Drain();
    REQUIRE( scene.Terrains.IsCollisionReady() ); REQUIRE( terrain.GetStreamingStats()->HorizonReady ); CHECK( terrain.GetStreamingStats()->CoverageEvicted > 0 );
    CHECK( terrain.GetData()->Heights->Query( 24, 24 ).Status == TerrainData::QueryStatus::Ready );
    RaycastHit hit; REQUIRE( scene.Physics.Raycast( Vector3( 26, 30, 24 ), Vector3( 0, -1, 0 ), 50, hit ) ); CHECK( hit.Entity == scene.Ground ); CHECK( hit.Position.y == doctest::Approx( 3 ) );
}


TEST_CASE( "Terrain prefetch: high-speed look-ahead stays optional under tight physics budgets" )
{
    bool paged = false; SUBCASE( "resident coverage" ) {} SUBCASE( "paged coverage" ) { paged = true; }
    StreamScene scene( paged ); auto& terrain = scene.Ground->GetComponent<Terrain>(); terrain.StreamDetail = false;
    auto& player = scene.Player->GetComponent<CharacterController>(); player.Teleport( { 8, 10, 8 }, Quaternion() ); player.MaxSpeed = 100;
    scene.GameWorld->Start(); scene.Drain(); REQUIRE( scene.Terrains.IsCollisionReady() ); REQUIRE( scene.Physics.IsTerrainCollisionReady() );
    CHECK( terrain.GetData()->CollisionTiles.size() == 1 ); CHECK( terrain.GetStreamingStats()->PrefetchResident == 0 );
    player.Teleport( { 8, 10, 8 }, Quaternion() ); player.SetMoveInput( { 1, 0, 0 } );
    for( int n = 0; n < 20; ++n ) { terrain.WaitForStreamingRequest(); scene.Physics.WaitForTerrainCollision(); scene.Step(); }
    CHECK( scene.Terrains.IsCollisionReady() ); CHECK( scene.Physics.IsTerrainCollisionReady() ); CHECK( terrain.GetStreamingStats()->PrefetchResident > 0 );
    CHECK( scene.Player->GetComponent<Transform>().GetWorldPosition().y < 10 ); CHECK( terrain.GetData()->CollisionTiles.size() == 1 );
    CHECK( terrain.GetStreamingStats()->CollisionReserved <= 1024 * 1024 ); CHECK( terrain.GetStreamingStats()->CpuReserved <= 1024 * 1024 );
    terrain.CollisionPrefetchSeconds = 0; player.SetMoveInput( {} ); player.Teleport( { 8, 10, 8 }, Quaternion() ); scene.Drain();
    CHECK( scene.Terrains.IsCollisionReady() ); CHECK( terrain.GetStreamingStats()->PrefetchResident == 0 ); CHECK( terrain.GetStreamingStats()->PrefetchPending == 0 );
}


TEST_CASE( "Terrain prefetch: required collision follows the real fixed step and an explicit sweep remains mandatory" )
{
    StreamScene scene; auto& terrain = scene.Ground->GetComponent<Terrain>(); terrain.CollisionPrefetchSeconds = 0;
    scene.Player->GetComponent<CharacterController>().Teleport( { 8, 10, 8 }, Quaternion() ); scene.GameWorld->Start(); scene.Drain(); REQUIRE( scene.Terrains.IsCollisionReady() );
    auto& player = scene.Player->GetComponent<CharacterController>(); player.MaxSpeed = 100;
    scene.Time.FixedDeltaTime = 0.2f; scene.Physics.FixedUpdate( scene.Time ); CHECK_FALSE( scene.Terrains.IsCollisionReady() );
    scene.Time.FixedDeltaTime = 0.001f;
    const auto settle = [&]() {
        for( int n = 0; n < 12; ++n ) { terrain.WaitForStreamingRequest(); scene.Physics.WaitForTerrainCollision(); scene.Physics.FixedUpdate( scene.Time ); }
    };
    settle(); REQUIRE( scene.Terrains.IsCollisionReady() );
    player.Move( { 24, 0, 0 } ); scene.Terrains.SyncNow( false, 0.001f ); CHECK_FALSE( scene.Terrains.IsCollisionReady() );
    player.Move( { -24, 0, 0 } ); settle(); CHECK( scene.Terrains.IsCollisionReady() );
}
