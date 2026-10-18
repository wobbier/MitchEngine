#include <doctest/doctest.h>

#include "Components/Graphics/Terrain.h"
#include "Components/Physics/CharacterController.h"
#include "Components/Physics/Colliders.h"
#include "Components/Physics/Rigidbody.h"
#include "Components/Transform.h"
#include "Cores/PhysicsCore.h"
#include "Cores/TerrainCore.h"
#include "Core/UpdateContext.h"
#include "Engine/World.h"
#include "Resource/ResourceCache.h"
#include "Resources/TerrainResource.h"
#include "Terrain/TerrainSerialization.h"
#include "World/SceneSerializer.h"
#include <cmath>
#include <filesystem>
#include <fstream>

namespace TerrainRuntimeTest
{
    TerrainData::Heightfield MakeField( float InHeight = 0.f )
    {
        TerrainData::Settings s;
        s.TileQuads = 4; s.PatchQuads = 2; s.MinTile = { -1, -1 }; s.TilesX = s.TilesZ = 2;
        TerrainData::Heightfield field( s );
        std::string error;
        for( int x = -1; x <= 0; ++x ) for( int z = -1; z <= 0; ++z ) REQUIRE( field.CreateFlatTile( { x, z }, InHeight, error ) );
        return field;
    }

    std::shared_ptr<const TerrainData::ResidentTerrain> MakeData( float InHeight = 0.f )
    {
        std::shared_ptr<const TerrainData::ResidentTerrain> data;
        std::string error;
        REQUIRE( TerrainData::BuildResidentTerrain( MakeField( InHeight ), 0x7465727261696e01ull, data, error ) );
        return data;
    }

    struct Context : UpdateContext
    {
        using UpdateContext::FixedDeltaTime;
        using UpdateContext::InterpolationAlpha;
        using UpdateContext::IsFixedStepActive;
    };

    struct Scene
    {
        TerrainCore Terrains;
        PhysicsCore Physics;
        std::shared_ptr<World> GameWorld = std::make_shared<World>();

        Scene()
        {
            GameWorld->IsLoading = false;
            GameWorld->AddCore( Terrains ); GameWorld->AddCore( Physics );
        }
        EntityHandle Create( const std::string& name, Vector3 position = Vector3() )
        {
            auto entity = GameWorld->CreateEntity( name );
            entity->AddComponent<Transform>().SetPosition( position );
            return entity;
        }
        EntityHandle Ground( std::shared_ptr<const TerrainData::ResidentTerrain> data = MakeData() )
        {
            auto ground = Create( "Terrain" ); ground->AddComponent<Terrain>().SetData( std::move( data ) ); return ground;
        }
        void Sync()
        {
            GameWorld->Simulate(); Terrains.SyncNow(); Physics.SyncNow();
            Context context; Physics.Update( context );
        }
        void Run( int steps )
        {
            Context context; context.FixedDeltaTime = 1.f / 60.f; context.InterpolationAlpha = 1.f;
            for( int i = 0; i < steps; ++i )
            {
                GameWorld->Simulate(); context.IsFixedStepActive = true; Physics.FixedUpdate( context );
                context.IsFixedStepActive = false; Physics.Update( context );
            }
        }
    };

    struct Files
    {
        std::filesystem::path Directory;
        Path Source;
        Path Cooked;
        Files()
        {
            static int sequence = 0;
            Directory = std::filesystem::path( ".tmp/Tests/TerrainResident" ) / std::to_string( ++sequence );
            std::filesystem::remove_all( Directory ); std::filesystem::create_directories( Directory / "pages" );
            auto field = MakeField( 2.f );
            TerrainData::Manifest manifest; manifest.AssetGUID = 0x7465727261696e01ull; manifest.Config = field.GetSettings();
            std::string error;
            for( const auto& [coord, tile] : field.GetTiles() )
            {
                std::string name = "pages/" + std::to_string( coord.X ) + "_" + std::to_string( coord.Z ) + ".thp";
                manifest.Pages.push_back( { coord, name } );
                std::vector<uint8_t> bytes;
                REQUIRE( TerrainData::SerializePage( field, coord, bytes, error ) );
                Write( Directory / name, bytes );
            }
            std::string text;
            REQUIRE( TerrainData::SerializeManifest( manifest, text, error ) );
            std::ofstream( Directory / "Test.terrain" ) << text;
            std::vector<uint8_t> cooked;
            REQUIRE( TerrainData::CookHeightfield( field, manifest.AssetGUID, TerrainData::HeightEncoding::UInt16, cooked, error ) );
            Write( Directory / "Test.terrainc", cooked );
            Source = Path( ( Directory / "Test.terrain" ).generic_string() );
            Cooked = Path( ( Directory / "Test.terrainc" ).generic_string() );
        }
        static void Write( const std::filesystem::path& path, const std::vector<uint8_t>& bytes )
        {
            std::ofstream file( path, std::ios::binary ); file.write( reinterpret_cast<const char*>( bytes.data() ), static_cast<std::streamsize>( bytes.size() ) );
        }
        ~Files()
        {
            ResourceCache::GetInstance().Evict( Source.FullPath ); ResourceCache::GetInstance().Evict( Cooked.FullPath );
            std::filesystem::remove_all( Directory );
        }
    };
}

using namespace TerrainRuntimeTest;

TEST_CASE( "Terrain: resident preparation shares an immutable CPU snapshot and matching collision tiles" )
{
    auto field = MakeField( 2.f );
    std::shared_ptr<const TerrainData::ResidentTerrain> data;
    std::string error;
    REQUIRE( TerrainData::BuildResidentTerrain( field, 123, data, error ) );
    CHECK( data->Patches.size() == 16 ); CHECK( data->CollisionTiles.size() == 4 );
    CHECK( data->Bounds.Min.x == -4.f ); CHECK( data->Bounds.Max.x == 4.f ); CHECK( data->Bounds.Min.y == 2.f );
    REQUIRE( field.SetSample( { 0, 0 }, 9.f, error ) );
    CHECK( data->Heights->Query( 0.0, 0.0 ).Height == 2.f );
    CHECK( field.Query( 0.0, 0.0 ).Height == 9.f );
    for( const auto& tile : data->CollisionTiles )
    {
        CHECK( tile.Positions.size() == 25 ); CHECK( tile.Indices.size() == 96 );
        CHECK( tile.Positions.front().x == tile.Coordinate.X * 4.f );
    }
    const auto original = data;
    field.UnloadTile( { 0, 0 } );
    CHECK_FALSE( TerrainData::BuildResidentTerrain( field, 123, data, error ) ); CHECK( data == original );
}

TEST_CASE( "Terrain: source and cooked resources prepare on the loader and publish only at finalization" )
{
    Files files;
    for( const Path& path : { files.Source, files.Cooked } )
    {
        TerrainResource resource( path );
        REQUIRE( resource.LoadAsync() ); CHECK( resource.GetData() == nullptr );
        REQUIRE( resource.FinishAsyncLoad() );
        auto data = resource.GetData(); REQUIRE( data != nullptr ); CHECK( data->Patches.size() == 16 );
        CHECK( data->Heights->Query( 0.0, 0.0 ).Height == doctest::Approx( 2.f ).epsilon( 0.01 ) );
    }
    auto cached = ResourceCache::GetInstance().GetAsync<TerrainResource>( files.Source );
    REQUIRE( cached != nullptr );
    ResourceCache::GetInstance().CompleteLoad( cached );
    CHECK_FALSE( cached->IsLoading() ); CHECK_FALSE( cached->HasLoadFailed() ); REQUIRE( cached->GetData() != nullptr );
    CHECK( ResourceCache::GetInstance().Get<TerrainResource>( files.Source ) == cached );
}

TEST_CASE( "Terrain: invalid source loads preserve a previous resource snapshot and report failure" )
{
    Files files;
    TerrainResource resource( files.Source ); REQUIRE( resource.Load() ); const auto original = resource.GetData();
    std::ofstream( files.Directory / "pages/-1_-1.thp", std::ios::trunc ) << "broken";
    CHECK_FALSE( resource.Load() ); CHECK( resource.GetData() == original ); CHECK_FALSE( resource.GetError().empty() );
    std::ofstream( files.Directory / "Test.terrain", std::ios::trunc );
    CHECK_FALSE( resource.Load() ); CHECK_FALSE( resource.GetError().empty() );
}

TEST_CASE( "Terrain: static collision matches translated height data and respects layer masks" )
{
    Scene scene;
    auto ground = scene.Ground( MakeData( 2.f ) );
    ground->GetComponent<Transform>().SetPosition( Vector3( 10.f, 1.f, 20.f ) ); ground->SetLayer( 3 );
    scene.Sync(); CHECK( scene.Physics.GetBodyCount() == 1 ); CHECK( scene.GameWorld->GetEntityCount() == 1 );
    RaycastHit hit;
    REQUIRE( scene.Physics.Raycast( Vector3( 10.f, 10.f, 20.f ), Vector3( 0.f, -1.f, 0.f ), 20.f, hit ) );
    CHECK( hit.Entity == ground ); CHECK( hit.Position.y == doctest::Approx( 3.f ) ); CHECK( hit.Normal.y > 0.99f );
    CHECK_FALSE( scene.Physics.Raycast( Vector3( 10.f, 10.f, 20.f ), Vector3( 0.f, -1.f, 0.f ), 20.f, hit, PhysicsLayers::Bit( 2 ) ) );
    ground->GetComponent<Transform>().SetPosition( Vector3( 10.f, 3.f, 20.f ) ); scene.Sync();
    REQUIRE( scene.Physics.Raycast( Vector3( 10.f, 10.f, 20.f ), Vector3( 0.f, -1.f, 0.f ), 20.f, hit ) ); CHECK( hit.Position.y == doctest::Approx( 5.f ) );
}

TEST_CASE( "Terrain: a body lands on the shared four-tile corner" )
{
    Scene scene; scene.Ground();
    auto box = scene.Create( "Box", Vector3( 0.f, 4.f, 0.f ) );
    box->AddComponent<Rigidbody>(); box->AddComponent<BoxCollider>();
    scene.GameWorld->Simulate(); scene.GameWorld->Start(); scene.Run( 180 );
    CHECK( box->GetComponent<Transform>().GetWorldPosition().y == doctest::Approx( 0.5f ).epsilon( 0.04 ) );
    CHECK( scene.GameWorld->GetEntityCount() == 2 );
}

TEST_CASE( "Terrain: a character crosses tile boundaries while remaining grounded" )
{
    Scene scene; scene.Ground();
    auto player = scene.Create( "Character", Vector3( -2.f, 2.f, -1.f ) );
    auto& controller = player->AddComponent<CharacterController>(); controller.MaxSpeed = 2.f;
    scene.GameWorld->Simulate(); scene.GameWorld->Start(); scene.Run( 90 );
    REQUIRE( controller.IsOnGround() ); controller.SetMoveInput( Vector3( 1.f, 0.f, 0.f ) ); scene.Run( 120 );
    CHECK( player->GetComponent<Transform>().GetWorldPosition().x > 1.f );
    CHECK( controller.IsOnGround() ); CHECK( std::abs( player->GetComponent<Transform>().GetWorldPosition().y - 0.9f ) < 0.1f );
}

TEST_CASE( "Terrain: replacing, disabling, and deleting an instance retires its collision safely" )
{
    Scene scene; auto ground = scene.Ground(); scene.Sync();
    auto replacement = MakeData( 3.f ); std::weak_ptr<const TerrainData::ResidentTerrain> lifetime = replacement;
    ground->GetComponent<Terrain>().SetData( replacement ); replacement.reset(); scene.Sync();
    RaycastHit hit;
    REQUIRE( scene.Physics.Raycast( Vector3( 1.f, 10.f, 1.f ), Vector3( 0.f, -1.f, 0.f ), 20.f, hit ) ); CHECK( hit.Position.y == doctest::Approx( 3.f ) );
    ground->GetComponent<Terrain>().Collision = false; scene.Sync(); CHECK( scene.Physics.GetBodyCount() == 0 );
    ground->GetComponent<Terrain>().Collision = true; scene.Sync(); CHECK( scene.Physics.GetBodyCount() == 1 );
    ground->GetComponent<Terrain>().SetEnabled( false ); scene.Sync(); CHECK( scene.Physics.GetBodyCount() == 0 );
    ground->GetComponent<Terrain>().SetEnabled( true ); scene.Sync(); CHECK( scene.Physics.GetBodyCount() == 1 );
    scene.GameWorld->DestroyEntity( ground ); scene.Sync(); CHECK( scene.Physics.GetBodyCount() == 0 ); CHECK( lifetime.expired() );
}

TEST_CASE( "Terrain: shared data survives unloading one instance and Play Stop cycles" )
{
    Scene scene; auto data = MakeData(); auto a = scene.Ground( data ); auto b = scene.Ground( data );
    b->GetComponent<Transform>().SetPosition( Vector3( 10.f, 0.f, 0.f ) ); scene.Sync();
    CHECK( scene.Physics.GetBodyCount() == 2 );
    scene.GameWorld->Start(); scene.Run( 2 ); scene.GameWorld->Stop();
    scene.GameWorld->DestroyEntity( a ); scene.Sync(); CHECK( scene.Physics.GetBodyCount() == 1 );
    RaycastHit hit;
    REQUIRE( scene.Physics.Raycast( Vector3( 10.f, 5.f, 0.f ), Vector3( 0.f, -1.f, 0.f ), 10.f, hit ) ); CHECK( hit.Entity == b );
    scene.GameWorld->Start(); scene.Run( 2 ); scene.GameWorld->Stop(); scene.Sync();
    CHECK( scene.Physics.GetBodyCount() == 1 );
}

TEST_CASE( "Terrain: unsupported inherited transforms and dynamic parents are visible failures" )
{
    Scene scene; auto parent = scene.Create( "Parent" ); auto ground = scene.Ground();
    ground->GetComponent<Transform>().SetParent( parent->GetComponent<Transform>() );
    parent->GetComponent<Transform>().SetScale( Vector3( 2.f ) ); scene.Sync();
    CHECK( ground->GetComponent<Terrain>().GetData() == nullptr ); CHECK_FALSE( ground->GetComponent<Terrain>().GetError().empty() ); CHECK( scene.Physics.GetBodyCount() == 0 );
    parent->GetComponent<Transform>().SetScale( Vector3( 1.f ) ); scene.Sync(); REQUIRE( ground->GetComponent<Terrain>().GetData() != nullptr );
    parent->AddComponent<Rigidbody>().Type = BodyType::Dynamic; scene.Sync();
    CHECK( ground->GetComponent<Terrain>().GetData() == nullptr );
    parent->GetComponent<Rigidbody>().Type = BodyType::Static; scene.Sync(); CHECK( ground->GetComponent<Terrain>().GetData() != nullptr );
}

TEST_CASE( "Terrain: scene serialization stores asset settings without generated patch entities" )
{
    Files files; Scene scene;
    auto ground = scene.Create( "Terrain" ); auto& terrain = ground->AddComponent<Terrain>();
    terrain.Asset = files.Source; terrain.Color = Vector3( 0.1f, 0.2f, 0.3f ); terrain.Collision = false;
    scene.GameWorld->Simulate(); scene.Terrains.SyncNow(); ResourceCache::GetInstance().WaitForAsyncLoads(); scene.Terrains.SyncNow();
    REQUIRE( terrain.GetData() != nullptr ); CHECK( scene.GameWorld->GetEntityCount() == 1 );
    const json saved = SceneSerializer::SerializeEntities( *scene.GameWorld, { ground.Get() } );
    REQUIRE( saved["Entities"].size() == 1 );
    auto copy = std::make_shared<World>(); copy->IsLoading = false;
    SceneSerializer::Deserialize( *copy, saved, {} ); copy->Simulate();
    CHECK( copy->GetEntityCount() == 1 );
    copy->Each<Terrain>( [&]( Entity&, Terrain& restored ) { CHECK( restored.Asset.GetLocalPathString() == terrain.Asset.GetLocalPathString() ); CHECK_FALSE( restored.Collision ); CHECK( restored.GetData() == nullptr ); CHECK( restored.Color.y == doctest::Approx( 0.2f ) ); } );
}

TEST_CASE( "Terrain: play-start readiness loads required resident terrain before physics queries" )
{
    Files files; Scene scene; auto ground = scene.Create( "Terrain" ); ground->AddComponent<Terrain>().Asset = files.Source;
    scene.GameWorld->Simulate(); scene.GameWorld->Start();
    REQUIRE( ground->GetComponent<Terrain>().GetData() != nullptr ); CHECK( scene.Physics.GetBodyCount() == 1 );
    RaycastHit hit; REQUIRE( scene.Physics.Raycast( Vector3( 1.f, 5.f, 1.f ), Vector3( 0.f, -1.f, 0.f ), 10.f, hit ) );
    CHECK( hit.Entity == ground ); CHECK( hit.Position.y == doctest::Approx( 2.f ) );
}

TEST_CASE( "Terrain: unavailable required ground holds physics until corrected" )
{
    Scene scene;
    auto ground = scene.Create( "Terrain" ); ground->AddComponent<Terrain>().Asset = Path( ".tmp/Tests/NoSuchTerrain.terrain" );
    auto box = scene.Create( "Box", Vector3( 1.f, 4.f, 1.f ) ); box->AddComponent<Rigidbody>(); box->AddComponent<BoxCollider>();
    scene.GameWorld->Simulate(); scene.GameWorld->Start(); scene.Run( 120 );
    CHECK_FALSE( scene.Terrains.IsCollisionReady() ); CHECK_FALSE( ground->GetComponent<Terrain>().GetError().empty() );
    CHECK( box->GetComponent<Transform>().GetWorldPosition().y == 4.f );
    ground->GetComponent<Terrain>().SetData( MakeData() ); scene.Run( 180 );
    CHECK( scene.Terrains.IsCollisionReady() ); CHECK( box->GetComponent<Transform>().GetWorldPosition().y == doctest::Approx( 0.5f ).epsilon( 0.04 ) );
}

TEST_CASE( "Terrain: a failed resident load can be retried after its source page is repaired" )
{
    Files files;
    const auto pagePath = files.Directory / "pages/-1_-1.thp";
    std::filesystem::remove( pagePath );
    Scene scene; auto ground = scene.Create( "Terrain" ); auto& terrain = ground->AddComponent<Terrain>(); terrain.Asset = files.Source;
    scene.GameWorld->Simulate(); scene.GameWorld->Start();
    CHECK_FALSE( scene.Terrains.IsCollisionReady() ); CHECK_FALSE( terrain.GetError().empty() );
    std::vector<uint8_t> bytes; std::string error;
    REQUIRE( TerrainData::SerializePage( MakeField( 2.f ), { -1, -1 }, bytes, error ) ); Files::Write( pagePath, bytes );
    terrain.ReloadAsset(); scene.Run( 1 );
    CHECK( scene.Terrains.IsCollisionReady() ); CHECK( terrain.GetData() != nullptr ); CHECK( scene.Physics.GetBodyCount() == 1 );
}
