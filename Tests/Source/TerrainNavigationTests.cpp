#include <doctest/doctest.h>
#include "Components/Graphics/Terrain.h"
#include "Components/Navigation/NavMeshSurface.h"
#include "Components/Navigation/NavMeshAgent.h"
#include "Components/Navigation/NavMeshModifiers.h"
#include "Components/Physics/Rigidbody.h"
#include "Components/Transform.h"
#include "Core/UpdateContext.h"
#include "Cores/NavigationCore.h"
#include "Engine/World.h"
#include "Navigation/TerrainNavGeometry.h"
#include "Terrain/TerrainStreamArchive.h"
#include "Terrain/TerrainSerialization.h"
#include "Terrain/TerrainSurface.h"
#include "Utils/GUID.h"
#include <fstream>

namespace TerrainNavigationTest
{
    using namespace TerrainData;
    struct Files
    {
        std::filesystem::path Root = std::filesystem::path( ".tmp/Tests/TerrainNavigation" ) / std::to_string( EngineGUID::Generate() );
        Files() { std::filesystem::create_directories( Root ); }


        ~Files() { std::error_code ec; std::filesystem::remove_all( Root, ec ); }
    };

    NavBuildSettings BakeSettings()
    {
        NavBuildSettings s; s.AgentRadius = 0.3f; s.CellSize = 0.3f; s.CellHeight = 0.1f; s.TileSize = 32;
        s.MinRegionSize = 1; s.MergeRegionSize = 2; return s;
    }


    Heightfield Field( bool slope = true, uint32_t tiles = 4 )
    {
        TerrainData::Settings s; s.TileQuads = 8; s.PatchQuads = 4; s.MinTile = { -int32_t( tiles / 2 ), -int32_t( tiles / 2 ) }; s.TilesX = s.TilesZ = tiles;
        Heightfield field( s ); std::string error;
        for( int x = s.MinTile.X; x < s.MinTile.X + int( tiles ); ++x ) for( int z = s.MinTile.Z; z < s.MinTile.Z + int( tiles ); ++z ) REQUIRE( field.CreateFlatTile( { x, z }, 0, error ) );
        if( slope ) for( int64_t z = s.MinSample().Z; z <= s.MaxSample().Z; ++z ) for( int64_t x = s.MinSample().X; x <= s.MaxSample().X; ++x )
            REQUIRE( field.SetSample( { x, z }, float( x * 0.1 + z * 0.02 + ( x == 1 && z == 1 ? 2 : 0 ) ), error ) );
        return field;
    }


    std::shared_ptr<const ResidentTerrain> Data( const Heightfield& field )
    {
        std::shared_ptr<const ResidentTerrain> data; std::string error; REQUIRE( BuildResidentTerrain( field, 123, data, error ) ); return data;
    }


    NavBuildInput Input( std::shared_ptr<const Heightfield> heights, std::shared_ptr<const StreamArchive> archive = {}, AABB bounds = {} )
    {
        NavBuildInput input; input.Bounds = bounds; NavTerrainSource source; std::string error;
        REQUIRE_MESSAGE( CaptureNavTerrain( heights, archive, {}, NavAreas::Walkable, bounds, BakeSettings(), source, error ), error );
        input.Terrains.push_back( std::move( source ) ); return input;
    }


    void SameGeometry( const NavBuildInput& a, const NavBuildInput& b )
    {
        REQUIRE( a.Vertices.size() == b.Vertices.size() ); CHECK( a.Indices == b.Indices ); CHECK( a.Areas == b.Areas );
        for( size_t i = 0; i < a.Vertices.size(); ++i ) CHECK( a.Vertices[i].InternalVector == b.Vertices[i].InternalVector );
    }


    void Flip( const std::filesystem::path& path, uint64_t offset )
    {
        std::fstream file( path, std::ios::binary | std::ios::in | std::ios::out ); file.seekg( offset ); char v; file.read( &v, 1 );
        v ^= 0x20; file.seekp( offset ); file.write( &v, 1 );
    }


    std::string Bytes( const std::filesystem::path& path )
    {
        std::ifstream file( path, std::ios::binary ); return { std::istreambuf_iterator<char>( file ), std::istreambuf_iterator<char>() };
    }


    struct Scene
    {
        NavigationCore Navigation;
        std::shared_ptr<World> WorldPtr = std::make_shared<World>();
        Scene() { WorldPtr->IsLoading = false; WorldPtr->AddCore( Navigation ); }


        EntityHandle Create( const std::string& name )
        {
            auto entity = WorldPtr->CreateEntity( name ); entity->AddComponent<Transform>(); return entity;
        }


        EntityHandle Ground( const Heightfield& field )
        {
            auto ground = Create( "Terrain" ); ground->AddComponent<Terrain>().SetData( Data( field ) ); return ground;
        }


        NavMeshSurface& Surface()
        {
            auto& surface = Create( "NavSurface" )->AddComponent<NavMeshSurface>();
            const auto s = BakeSettings(); surface.AgentRadius = s.AgentRadius; surface.CellSize = s.CellSize; surface.CellHeight = s.CellHeight;
            surface.TileSize = s.TileSize; surface.MinRegionSize = 1; surface.MergeRegionSize = 2; WorldPtr->Simulate(); return surface;
        }
    };
}
using namespace TerrainNavigationTest;

TEST_CASE( "Terrain navigation: canonical triangles retain fine peaks and the height-query diagonal" )
{
    auto field = std::make_shared<Heightfield>( Field() ); auto input = Input( field ); std::string error;
    CHECK( input.Vertices.empty() ); REQUIRE( AppendNavTerrainGeometry( input, error ) );
    CHECK( input.Areas.size() == 32 * 32 * 2 ); CHECK( input.Terrains.empty() ); bool peak = false;
    for( const auto& v : input.Vertices ) if( v.x == 1 && v.z == 1 ) { CHECK( v.y == doctest::Approx( 2.12 ) ); peak = true; }
    CHECK( peak );
    for( size_t i = 0; i < input.Indices.size(); i += 3 )
    {
        const auto& a = input.Vertices[input.Indices[i]]; const auto& b = input.Vertices[input.Indices[i + 1]]; const auto& c = input.Vertices[input.Indices[i + 2]];
        CHECK( ( b - a ).Cross( c - a ).y > 0 );
        const auto mid = ( a + b + c ) / 3.f; const auto query = field->Query( mid.x, mid.z );
        REQUIRE( query.Status == QueryStatus::Ready ); CHECK( query.Height == doctest::Approx( mid.y ).epsilon( 0.0001 ) );
    }
}


TEST_CASE( "Terrain navigation: resident and all stream formats bake identical geometry across signed tile borders" )
{
    Files files; auto field = Field(); auto source = Input( std::make_shared<Heightfield>( field ) );
    auto expected = source; std::string error; REQUIRE( AppendNavTerrainGeometry( expected, error ) );
    const auto baseline = BuildNavMesh( source, BakeSettings() ); REQUIRE_MESSAGE( baseline.Success, baseline.Error );
    std::vector<uint8_t> bytes; REQUIRE( CookHeightfield( field, 123, HeightEncoding::Float32, bytes, error ) );
    Heightfield decoded; CookInfo info; REQUIRE( ReadCookedHeightfield( bytes, decoded, info, error ) );
    auto resident = Input( std::make_shared<Heightfield>( decoded ) ); REQUIRE( AppendNavTerrainGeometry( resident, error ) ); SameGeometry( resident, expected );
    for( int version : { 2, 3, 4 } )
    {
        if( version == 4 ) { auto surface = std::make_shared<Surface>(); surface->Set( { 1, 1 }, { 0, 0, 255, 0 } ); REQUIRE( field.SetSurface( surface, error ) ); }
        const auto path = files.Root / "Runtime.terrainc";
        REQUIRE( ( version == 2 ? CookStreamArchive( path, field, 123, 2, error ) : CookPagedStreamArchive( path, field, 123, 2, error ) ) );
        std::shared_ptr<const StreamArchive> archive; REQUIRE( OpenStreamArchive( path, archive, error ) );
        auto input = Input( archive->Coverage->Heights, archive ); CHECK( archive->Coverage->Heights->GetTiles().empty() );
        auto expanded = input; REQUIRE( AppendNavTerrainGeometry( expanded, error ) ); SameGeometry( expanded, expected );
        const auto baked = BuildNavMesh( input, BakeSettings() ); REQUIRE_MESSAGE( baked.Success, baked.Error ); CHECK( baked.Data.Tiles == baseline.Data.Tiles );
        NavMesh mesh; REQUIRE( mesh.Load( baked.Data ) ); std::vector<Vector3> corners;
        CHECK( mesh.FindPath( { -10, -1.2f, -10 }, { 10, 1.2f, 10 }, corners ) == NavPathStatus::Complete );
    }
}


TEST_CASE( "Terrain navigation: collection volumes read only intersecting canonical pages independently of coverage" )
{
    Files files; auto field = Field( false, 16 ); const auto path = files.Root / "Paged.terrainc"; std::string error;
    REQUIRE( CookPagedStreamArchive( path, field, 123, 2, error ) ); std::shared_ptr<const StreamArchive> archive; REQUIRE( OpenStreamArchive( path, archive, error ) );
    AABB bounds; bounds.Min = { -2, -2, -2 }; bounds.Max = { 2, 2, 2 };
    auto input = Input( archive->Coverage->Heights, archive, bounds ); REQUIRE( input.Terrains[0].Regions.size() < 64 );
    const auto& distant = archive->Blocks[archive->TileIndex( { 7, 7 } )]; Flip( path, distant.Offset );
    const auto& center = archive->Blocks[archive->TileIndex( { 0, 0 } )]; Flip( path, center.CoverageOffset );
    CHECK( BuildNavMesh( input, BakeSettings() ).Success );
    const auto originalHash = input.Hash(); const auto saved = files.Root / "Held.terrainc"; std::filesystem::rename( path, saved );
    CHECK( Input( archive->Coverage->Heights, archive, bounds ).Hash() == originalHash ); // Capture has no file I/O.
    CHECK_FALSE( BuildNavMesh( input, BakeSettings() ).Success ); std::filesystem::rename( saved, path );
    Flip( path, center.Offset ); auto failed = BuildNavMesh( input, BakeSettings() ); CHECK_FALSE( failed.Success ); CHECK( failed.Error.find( "checksum" ) != std::string::npos );
    std::vector<float> preserved{ 123 }; CHECK_FALSE( ReadStreamHeights( *archive, { 0, 0 }, preserved, error ) ); CHECK( preserved == std::vector<float>{ 123 } );
}


TEST_CASE( "Terrain navigation: layers children modifiers translation and collision source choice apply once" )
{
    Scene scene; auto field = Field( false ); auto ground = scene.Ground( field ); auto& surface = scene.Surface();
    auto& terrain = ground->GetComponent<Terrain>();
    for( auto mode : { NavCollectGeometry::PhysicsColliders, NavCollectGeometry::RenderMeshes, NavCollectGeometry::Both } )
    { surface.UseGeometry = mode; CHECK( scene.Navigation.GatherInput( surface ).Terrains.size() == 1 ); }
    terrain.Collision = false; surface.UseGeometry = NavCollectGeometry::PhysicsColliders; CHECK( scene.Navigation.GatherInput( surface ).Terrains.empty() );
    surface.UseGeometry = NavCollectGeometry::RenderMeshes; CHECK( scene.Navigation.GatherInput( surface ).Terrains.size() == 1 );
    ground->SetLayer( 3 ); surface.IncludeLayers = 1; CHECK( scene.Navigation.GatherInput( surface ).Terrains.empty() ); surface.IncludeLayers = 1u << 3;
    surface.Collect = NavCollectObjects::Children; CHECK( scene.Navigation.GatherInput( surface ).Terrains.empty() );
    ground->GetComponent<Transform>().SetParent( surface.Parent->GetComponent<Transform>() );
    surface.Parent->GetComponent<Transform>().SetPosition( { 10, 3, -20 } );
    auto& modifier = surface.Parent->AddComponent<NavMeshModifier>(); modifier.Area = 3;
    scene.WorldPtr->Simulate(); auto input = scene.Navigation.GatherInput( surface ); REQUIRE( input.Terrains.size() == 1 );
    CHECK( input.Terrains[0].Translation.InternalVector == Vector3( 10, 3, -20 ).InternalVector );
    std::string error; REQUIRE( AppendNavTerrainGeometry( input, error ) ); for( auto area : input.Areas ) CHECK( area == 3 );
    modifier.IgnoreFromBuild = true; CHECK( scene.Navigation.GatherInput( surface ).Terrains.empty() ); modifier.IgnoreFromBuild = false;
    terrain.SetEnabled( false ); CHECK( scene.Navigation.GatherInput( surface ).Terrains.empty() ); terrain.SetEnabled( true );
    ground->AddComponent<Rigidbody>().Type = BodyType::Static;
    auto parent = scene.Create( "Moving parent" ); parent->AddComponent<Rigidbody>().Type = BodyType::Kinematic;
    ground->GetComponent<Transform>().SetParent( parent->GetComponent<Transform>() ); surface.Collect = NavCollectObjects::All;
    CHECK( scene.Navigation.GatherInput( surface ).Terrains.empty() );
}


TEST_CASE( "Terrain navigation: height edits mark stale paint and LOD changes do not and stale workers retain the baked file" )
{
    Files files; Scene scene; auto field = Field( false ); auto ground = scene.Ground( field ); auto& surface = scene.Surface();
    surface.NavMeshData = ( files.Root / "Terrain.navmesh" ).generic_string();
    REQUIRE( scene.Navigation.Bake( surface, false ) ); REQUIRE( scene.Navigation.GetNavMesh( surface ) ); CHECK_FALSE( scene.Navigation.IsOutOfDate( surface, true ) );
    auto& terrain = ground->GetComponent<Terrain>(); terrain.ForcedLod = 3; terrain.StreamDetail = false; terrain.CoverageDistance = 1;
    CHECK_FALSE( scene.Navigation.IsOutOfDate( surface, true ) );
    std::string error; auto surfaceData = std::make_shared<Surface>(); surfaceData->Layers[0].Roughness = 0.2f; REQUIRE( field.SetSurface( surfaceData, error ) );
    terrain.SetData( Data( field ) ); CHECK_FALSE( scene.Navigation.IsOutOfDate( surface, true ) );
    const auto hash = scene.Navigation.GetNavMesh( surface )->GetData().SourceHash; const auto file = Bytes( files.Root / "Terrain.navmesh" );
    REQUIRE( field.SetSample( { 0, 0 }, 2, error ) ); terrain.SetData( Data( field ) ); CHECK( scene.Navigation.IsOutOfDate( surface, true ) );
    REQUIRE( scene.Navigation.Bake( surface, true ) );
    REQUIRE( field.SetSample( { 0, 0 }, 4, error ) ); terrain.SetData( Data( field ) ); scene.Navigation.WaitForBakes();
    CHECK( scene.Navigation.GetNavMesh( surface )->GetData().SourceHash == hash ); CHECK( Bytes( files.Root / "Terrain.navmesh" ) == file );
    CHECK( scene.Navigation.GetSurfaceInfo( surface ).LastError.find( "changed" ) != std::string::npos );
    REQUIRE( scene.Navigation.Bake( surface, false ) ); CHECK_FALSE( scene.Navigation.IsOutOfDate( surface, true ) );
}


TEST_CASE( "Terrain navigation: missing invalid and cancelled sources fail without replacing a valid navmesh" )
{
    Scene scene; auto ground = scene.Ground( Field( false ) ); auto& surface = scene.Surface(); REQUIRE( scene.Navigation.Bake( surface, false ) );
    const auto* mesh = scene.Navigation.GetNavMesh( surface ); REQUIRE( mesh ); const auto hash = mesh->GetData().SourceHash;
    ground->GetComponent<Terrain>().SetData( nullptr ); CHECK_FALSE( scene.Navigation.Bake( surface, false ) ); CHECK( scene.Navigation.GetNavMesh( surface ) == mesh );
    ground->GetComponent<Terrain>().SetData( Data( Field( false ) ) ); ground->GetComponent<Transform>().SetScale( { 2, 1, 1 } );
    CHECK_FALSE( scene.Navigation.Bake( surface, false ) ); CHECK( scene.Navigation.GetNavMesh( surface )->GetData().SourceHash == hash );
    auto input = Input( std::make_shared<Heightfield>( Field( false ) ) ); NavBuildOptions options; std::atomic<bool> cancel{ true }; options.Cancel = &cancel;
    const auto result = BuildNavMesh( input, BakeSettings(), options ); CHECK_FALSE( result.Success ); CHECK( result.Error == "cancelled" );
    TerrainData::Settings huge; huge.TileQuads = 4096; huge.PatchQuads = 64; NavTerrainSource source; std::string error;
    CHECK_FALSE( CaptureNavTerrain( std::make_shared<Heightfield>( huge ), {}, {}, 0, {}, BakeSettings(), source, error ) ); CHECK( error.find( "four million" ) != std::string::npos );
}


TEST_CASE( "Terrain navigation: agents and runtime obstacle carving use canonical terrain and Stop restores the bake" )
{
    Scene scene; scene.Ground( Field( false ) ); auto& surface = scene.Surface(); REQUIRE( scene.Navigation.Bake( surface, false ) );
    auto walker = scene.Create( "Walker" ); walker->GetComponent<Transform>().SetPosition( { -8, 0, -8 } ); auto& agent = walker->AddComponent<NavMeshAgent>();
    agent.Speed = 8; agent.Acceleration = 40; agent.Radius = 0.3f;
    auto box = scene.Create( "Obstacle" ); box->GetComponent<Transform>().SetPosition( { 0, 0.5f, 0 } );
    auto& obstacle = box->AddComponent<NavMeshObstacle>(); obstacle.Size = { 8, 2, 2 }; obstacle.CarveDelay = 0;
    scene.WorldPtr->Simulate(); scene.WorldPtr->Start(); UpdateContext context;
    for( int n = 0; n < 3; ++n ) { scene.WorldPtr->Simulate(); scene.Navigation.Update( context ); } scene.Navigation.WaitForBakes();
    NavMeshHit hit; CHECK_FALSE( scene.Navigation.SamplePosition( {}, 0.2f, hit ) );
    agent.SetDestination( { 8, 0, 8 } );
    for( int n = 0; n < 600 && !agent.HasArrived(); ++n ) { scene.WorldPtr->Simulate(); scene.Navigation.Update( context ); }
    CHECK( agent.HasArrived() ); scene.WorldPtr->Stop(); CHECK( scene.Navigation.SamplePosition( {}, 0.2f, hit ) );
}


TEST_CASE( "Terrain navigation: loading a different baked file replaces the captured carving source" )
{
    Files files; Scene scene; auto field = Field( false ); auto ground = scene.Ground( field ); auto& surface = scene.Surface();
    surface.NavMeshData = ( files.Root / "First.navmesh" ).generic_string(); REQUIRE( scene.Navigation.Bake( surface, false ) );
    std::string error;
    for( int z = -16; z <= 16; ++z ) for( int x = -16; x <= 16; ++x ) REQUIRE( field.SetSample( { x, z }, 2, error ) );
    const auto input = Input( std::make_shared<Heightfield>( field ) ); const auto baked = BuildNavMesh( input, surface.GetBuildSettings() );
    REQUIRE_MESSAGE( baked.Success, baked.Error ); const auto second = files.Root / "Second.navmesh"; REQUIRE( baked.Data.Save( second.string() ) );
    ground->GetComponent<Terrain>().SetData( Data( field ) ); surface.NavMeshData = second.generic_string(); scene.Navigation.SyncNow();
    CHECK_FALSE( scene.Navigation.IsOutOfDate( surface, true ) );
    auto box = scene.Create( "Obstacle" ); box->GetComponent<Transform>().SetPosition( { 0, 2.5f, 0 } );
    auto& obstacle = box->AddComponent<NavMeshObstacle>(); obstacle.Size = { 2, 2, 2 }; obstacle.CarveDelay = 0;
    scene.WorldPtr->Simulate(); scene.WorldPtr->Start(); UpdateContext context;
    for( int n = 0; n < 3; ++n ) { scene.WorldPtr->Simulate(); scene.Navigation.Update( context ); } scene.Navigation.WaitForBakes();
    NavMeshHit hit; CHECK_FALSE( scene.Navigation.SamplePosition( { 0, 2, 0 }, 0.2f, hit ) );
    REQUIRE( scene.Navigation.SamplePosition( { 3, 2, 3 }, 0.2f, hit ) ); CHECK( hit.Position.y == doctest::Approx( 2 ).epsilon( 0.1 ) );
    scene.WorldPtr->Stop(); CHECK( scene.Navigation.SamplePosition( { 0, 2, 0 }, 0.2f, hit ) );
}
