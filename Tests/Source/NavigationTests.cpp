#include <doctest/doctest.h>
#include "Components/Navigation/NavMeshAgent.h"
#include "Components/Navigation/NavMeshModifiers.h"
#include "Components/Navigation/NavMeshSurface.h"
#include "Components/Physics/Colliders.h"
#include "Components/Physics/Rigidbody.h"
#include "Components/Transform.h"
#include "Core/UpdateContext.h"
#include "Cores/NavigationCore.h"
#include "Engine/ProjectSettings.h"
#include "Engine/World.h"
#include "Navigation/NavMesh.h"
#include "Navigation/NavMeshBuilder.h"
#include <cmath>
#include <memory>

namespace NavigationTest
{
    // Axis-aligned box with outward-facing triangles.
    void AddBox( NavBuildInput& InInput, const Vector3& InMin, const Vector3& InMax, uint8_t InArea = NavAreas::Walkable )
    {
        Vector3 corners[8];
        for( int i = 0; i < 8; ++i )
        {
            corners[i] = Vector3( ( i & 1 ) ? InMax.x : InMin.x, ( i & 2 ) ? InMax.y : InMin.y, ( i & 4 ) ? InMax.z : InMin.z );
        }
        static const int kFaces[6][4] = { { 2, 6, 7, 3 }, { 0, 1, 5, 4 }, { 1, 3, 7, 5 }, { 0, 4, 6, 2 }, { 4, 5, 7, 6 }, { 0, 2, 3, 1 } };
        for( const auto& face : kFaces )
        {
            InInput.AddTriangle( corners[face[0]], corners[face[1]], corners[face[2]], InArea );
            InInput.AddTriangle( corners[face[0]], corners[face[2]], corners[face[3]], InArea );
        }
    }

    // Flat, upward-facing floor quad at height InY.
    void AddFloor( NavBuildInput& InInput, float InMinX, float InMinZ, float InMaxX, float InMaxZ, float InY = 0.f, uint8_t InArea = NavAreas::Walkable )
    {
        const Vector3 a( InMinX, InY, InMinZ );
        const Vector3 b( InMinX, InY, InMaxZ );
        const Vector3 c( InMaxX, InY, InMinZ );
        const Vector3 d( InMaxX, InY, InMaxZ );
        InInput.AddTriangle( a, b, c, InArea );
        InInput.AddTriangle( c, b, d, InArea );
    }

    float PathLength( const std::vector<Vector3>& InCorners )
    {
        float length = 0.f;
        for( size_t i = 1; i < InCorners.size(); ++i )
        {
            length += ( InCorners[i] - InCorners[i - 1] ).Length();
        }
        return length;
    }

    NavBuildSettings FastSettings()
    {
        NavBuildSettings settings;
        settings.AgentRadius = 0.4f;
        settings.CellSize = 0.2f;
        settings.CellHeight = 0.1f;
        settings.TileSize = 48;
        return settings;
    }

    std::unique_ptr<NavMesh> Bake( const NavBuildInput& InInput, const NavBuildSettings& InSettings = FastSettings() )
    {
        NavBuildResult result = BuildNavMesh( InInput, InSettings );
        DOCTEST_INFO( result.Error );
        REQUIRE( result.Success );
        auto mesh = std::make_unique<NavMesh>();
        REQUIRE( mesh->Load( result.Data ) );
        return mesh;
    }

    struct TestContext
        : public UpdateContext
    {
    };

    // A world with the navigation core. The core is declared first so it outlives the world.
    struct Scene
    {
        NavigationCore Navigation;
        std::shared_ptr<World> GameWorld;

        Scene()
        {
            GameWorld = std::make_shared<World>();
            GameWorld->IsLoading = false;
            GameWorld->AddCore( Navigation );
        }

        EntityHandle Create( const std::string& InName, const Vector3& InPosition )
        {
            EntityHandle entity = GameWorld->CreateEntity( InName );
            entity->AddComponent<Transform>().SetPosition( InPosition );
            return entity;
        }

        EntityHandle Box( const std::string& InName, const Vector3& InCenter, const Vector3& InSize )
        {
            EntityHandle entity = Create( InName, InCenter );
            entity->AddComponent<BoxCollider>().Size = InSize;
            return entity;
        }

        NavMeshSurface& Surface()
        {
            EntityHandle entity = Create( "Surface", Vector3( 0.f, 0.f, 0.f ) );
            NavMeshSurface& surface = entity->AddComponent<NavMeshSurface>();
            surface.AgentRadius = 0.4f;
            surface.CellSize = 0.2f;
            surface.CellHeight = 0.1f;
            GameWorld->Simulate();
            return surface;
        }

        void Run( float InSeconds )
        {
            TestContext context;
            const int frames = static_cast<int>( std::lround( InSeconds * 60.f ) );
            for( int i = 0; i < frames; ++i )
            {
                GameWorld->Simulate();
                Navigation.Update( context );
            }
        }
    };
}

using namespace NavigationTest;

TEST_CASE( "Navigation: a path goes around a wall" )
{
    NavBuildInput input;
    AddFloor( input, -10.f, -10.f, 10.f, 10.f );
    AddBox( input, Vector3( -5.f, 0.f, -0.5f ), Vector3( 5.f, 2.f, 0.5f ) );
    std::unique_ptr<NavMesh> mesh = Bake( input );
    CHECK( mesh->GetPolyCount() > 0 );

    std::vector<Vector3> corners;
    REQUIRE( mesh->FindPath( Vector3( 0.f, 0.f, -5.f ), Vector3( 0.f, 0.f, 5.f ), corners ) == NavPathStatus::Complete );
    REQUIRE( corners.size() >= 3 );
    CHECK( PathLength( corners ) > 10.5f );   // the straight line is blocked
    for( const Vector3& corner : corners )
    {
        // No corner inside the wall (inflated by the agent radius).
        CHECK_FALSE( ( std::abs( corner.x ) < 5.3f && std::abs( corner.z ) < 0.85f ) );
    }
    CHECK( corners.front().z == doctest::Approx( -5.f ).epsilon( 0.05 ) );
    CHECK( corners.back().z == doctest::Approx( 5.f ).epsilon( 0.05 ) );

    // The surface sits on the floor, not on the wall top.
    NavMeshHit hit;
    REQUIRE( mesh->SamplePosition( Vector3( 3.f, 1.f, 4.f ), 2.f, hit ) );
    CHECK( hit.Position.y == doctest::Approx( 0.f ).epsilon( 0.2 ) );
    CHECK( hit.Area == NavAreas::Walkable );

    // A raycast along the floor stops at the wall.
    NavMeshHit wall;
    CHECK( mesh->Raycast( Vector3( 0.f, 0.f, -5.f ), Vector3( 0.f, 0.f, 5.f ), wall ) );
    CHECK( wall.Hit );
    CHECK( wall.Position.z < -0.5f );
    CHECK( wall.Position.z > -1.5f );
    CHECK_FALSE( mesh->Raycast( Vector3( -8.f, 0.f, -5.f ), Vector3( -8.f, 0.f, 5.f ), wall ) );
}


TEST_CASE( "Navigation: large areas bake as many tiles and round-trip through a file" )
{
    NavBuildInput input;
    AddFloor( input, -30.f, -30.f, 30.f, 30.f );
    NavBuildSettings settings = FastSettings();
    settings.TileSize = 32;
    NavBuildResult result = BuildNavMesh( input, settings );
    REQUIRE( result.Success );
    CHECK( result.TileCount > 4 );
    CHECK( result.PolyCount >= result.TileCount );

    const std::vector<uint8_t> bytes = result.Data.Serialize();
    NavMeshData copy;
    REQUIRE( copy.Deserialize( bytes.data(), bytes.size() ) );
    CHECK( copy.Tiles.size() == result.Data.Tiles.size() );
    CHECK( copy.SourceHash == result.Data.SourceHash );
    CHECK_FALSE( copy.Deserialize( bytes.data(), bytes.size() / 2 ) );   // truncated

    REQUIRE( copy.Deserialize( bytes.data(), bytes.size() ) );
    NavMesh mesh;
    REQUIRE( mesh.Load( copy ) );
    std::vector<Vector3> corners;
    // Crosses many tile borders in a straight line.
    REQUIRE( mesh.FindPath( Vector3( -25.f, 0.f, -25.f ), Vector3( 25.f, 0.f, 25.f ), corners ) == NavPathStatus::Complete );
    CHECK( PathLength( corners ) == doctest::Approx( std::sqrt( 2.f ) * 50.f ).epsilon( 0.02 ) );

    // Same inputs, same hash; different settings, different hash.
    CHECK( BuildNavMesh( input, settings ).Data.SourceHash == result.Data.SourceHash );
    settings.AgentRadius = 0.6f;
    CHECK( BuildNavMesh( input, settings ).Data.SourceHash != result.Data.SourceHash );
}


TEST_CASE( "Navigation: areas, costs and masks shape paths" )
{
    // A strip of area 3 across the middle of the floor, except at the far ends.
    NavBuildInput input;
    AddFloor( input, -10.f, -10.f, 10.f, 10.f );
    NavBuildInput::Volume swamp;
    swamp.Outline = { Vector3( -7.f, 0.f, -1.f ), Vector3( 7.f, 0.f, -1.f ), Vector3( 7.f, 0.f, 1.f ), Vector3( -7.f, 0.f, 1.f ) };
    swamp.MinY = -1.f;
    swamp.MaxY = 1.f;
    swamp.Area = 3;
    input.Volumes.push_back( swamp );
    std::unique_ptr<NavMesh> mesh = Bake( input );

    NavMeshHit hit;
    REQUIRE( mesh->SamplePosition( Vector3( 0.f, 0.f, 0.f ), 0.5f, hit ) );
    CHECK( hit.Area == 3 );

    ProjectSettings& settings = ProjectSettings::Get();
    const float oldCost = settings.NavAreaCosts[3];
    std::vector<Vector3> corners;

    settings.NavAreaCosts[3] = 1.f;
    REQUIRE( mesh->FindPath( Vector3( 0.f, 0.f, -5.f ), Vector3( 0.f, 0.f, 5.f ), corners ) == NavPathStatus::Complete );
    CHECK( PathLength( corners ) == doctest::Approx( 10.f ).epsilon( 0.05 ) );   // straight through

    settings.NavAreaCosts[3] = 50.f;
    REQUIRE( mesh->FindPath( Vector3( 0.f, 0.f, -5.f ), Vector3( 0.f, 0.f, 5.f ), corners ) == NavPathStatus::Complete );
    CHECK( PathLength( corners ) > 14.f );   // around the end of the strip

    // Excluding the area altogether also detours.
    settings.NavAreaCosts[3] = 1.f;
    NavQueryFilter dry;
    dry.AreaMask = NavAreas::All & ~NavAreas::Bit( 3 );
    REQUIRE( mesh->FindPath( Vector3( 0.f, 0.f, -5.f ), Vector3( 0.f, 0.f, 5.f ), corners, dry ) == NavPathStatus::Complete );
    CHECK( PathLength( corners ) > 14.f );
    settings.NavAreaCosts[3] = oldCost;

    // A Not Walkable volume across the whole floor cuts it in two.
    NavBuildInput cut = input;
    cut.Volumes[0].Outline = { Vector3( -11.f, 0.f, -1.f ), Vector3( 11.f, 0.f, -1.f ), Vector3( 11.f, 0.f, 1.f ), Vector3( -11.f, 0.f, 1.f ) };
    cut.Volumes[0].Area = NavAreas::NotWalkable;
    std::unique_ptr<NavMesh> split = Bake( cut );
    CHECK( split->FindPath( Vector3( 0.f, 0.f, -5.f ), Vector3( 0.f, 0.f, 5.f ), corners ) == NavPathStatus::Partial );
}


TEST_CASE( "Navigation: slopes steeper than the limit aren't walkable" )
{
    auto rampTop = []( float InDegrees ) {
        // A 4 m long ramp rising from z = 0, plus a floor at its foot.
        NavBuildInput input;
        AddFloor( input, -4.f, -6.f, 4.f, 0.f );
        const float rise = std::tan( InDegrees * 3.14159265f / 180.f ) * 4.f;
        const Vector3 a( -4.f, 0.f, 0.f );
        const Vector3 b( -4.f, rise, 4.f );
        const Vector3 c( 4.f, 0.f, 0.f );
        const Vector3 d( 4.f, rise, 4.f );
        input.AddTriangle( a, b, c, NavAreas::Walkable );
        input.AddTriangle( c, b, d, NavAreas::Walkable );
        NavBuildSettings settings = FastSettings();
        settings.AgentMaxSlope = 45.f;
        NavBuildResult result = BuildNavMesh( input, settings );
        REQUIRE( result.Success );
        NavMesh mesh;
        REQUIRE( mesh.Load( result.Data ) );
        NavMeshHit hit;
        return mesh.SamplePosition( Vector3( 0.f, rise * 0.75f, 3.f ), 0.3f, hit );
    };
    CHECK( rampTop( 30.f ) );
    CHECK_FALSE( rampTop( 60.f ) );
}


TEST_CASE( "Navigation: an off-mesh link connects two islands" )
{
    NavBuildInput input;
    AddFloor( input, -4.f, -10.f, 4.f, -2.f );
    AddFloor( input, -4.f, 2.f, 4.f, 10.f );
    {
        std::unique_ptr<NavMesh> mesh = Bake( input );
        std::vector<Vector3> corners;
        CHECK( mesh->FindPath( Vector3( 0.f, 0.f, -6.f ), Vector3( 0.f, 0.f, 6.f ), corners ) == NavPathStatus::Partial );
    }
    NavBuildInput::Link link;
    link.Start = Vector3( 0.f, 0.f, -2.8f );
    link.End = Vector3( 0.f, 0.f, 2.8f );
    link.Radius = 0.5f;
    input.Links.push_back( link );
    std::unique_ptr<NavMesh> mesh = Bake( input );
    std::vector<Vector3> corners;
    REQUIRE( mesh->FindPath( Vector3( 0.f, 0.f, -6.f ), Vector3( 0.f, 0.f, 6.f ), corners ) == NavPathStatus::Complete );
    int linkCount = 0;
    mesh->ForEachLink( [&]( const Vector3&, const Vector3&, float, bool bidirectional ) {
        ++linkCount;
        CHECK( bidirectional );
    } );
    CHECK( linkCount == 1 );
    // And back (bidirectional).
    CHECK( mesh->FindPath( Vector3( 0.f, 0.f, 6.f ), Vector3( 0.f, 0.f, -6.f ), corners ) == NavPathStatus::Complete );
}


TEST_CASE( "Navigation: empty or unwalkable input fails with a reason" )
{
    NavBuildInput empty;
    NavBuildResult result = BuildNavMesh( empty, FastSettings() );
    CHECK_FALSE( result.Success );
    CHECK_FALSE( result.Error.empty() );

    // Only a ceiling (facing down): nothing to stand on.
    NavBuildInput ceiling;
    ceiling.AddTriangle( Vector3( -5.f, 3.f, -5.f ), Vector3( 5.f, 3.f, -5.f ), Vector3( -5.f, 3.f, 5.f ), NavAreas::Walkable );
    result = BuildNavMesh( ceiling, FastSettings() );
    CHECK_FALSE( result.Success );
}


TEST_CASE( "Navigation: the core bakes scene colliders and agents walk around obstacles" )
{
    Scene scene;
    scene.Box( "Ground", Vector3( 0.f, -0.5f, 0.f ), Vector3( 24.f, 1.f, 24.f ) );
    scene.Box( "Wall", Vector3( 0.f, 1.f, 0.f ), Vector3( 12.f, 2.f, 1.f ) );
    // Moving things and ignored geometry stay out of the navmesh.
    EntityHandle crate = scene.Box( "Crate", Vector3( -8.f, 0.5f, -4.f ), Vector3( 2.f, 1.f, 2.f ) );
    crate->AddComponent<Rigidbody>().Type = BodyType::Dynamic;
    EntityHandle ghost = scene.Box( "Ghost", Vector3( 8.f, 0.5f, -4.f ), Vector3( 2.f, 1.f, 2.f ) );
    ghost->AddComponent<NavMeshModifier>().IgnoreFromBuild = true;

    NavMeshSurface& surface = scene.Surface();
    const NavBuildInput input = scene.Navigation.GatherInput( surface );
    CHECK( input.Areas.size() == 24 );   // ground + wall

    REQUIRE( scene.Navigation.Bake( surface, false ) );
    REQUIRE( scene.Navigation.GetNavMesh( surface ) != nullptr );

    NavMeshHit hit;
    CHECK( scene.Navigation.SamplePosition( Vector3( -8.f, 0.f, -4.f ), 0.3f, hit ) );   // under the crate: walkable
    CHECK( scene.Navigation.SamplePosition( Vector3( 8.f, 0.f, -4.f ), 0.3f, hit ) );    // under the ghost: walkable

    EntityHandle walker = scene.Create( "Walker", Vector3( 0.f, 0.f, -6.f ) );
    NavMeshAgent& agent = walker->AddComponent<NavMeshAgent>();
    agent.Radius = 0.4f;
    agent.Speed = 6.f;
    agent.Acceleration = 30.f;
    scene.GameWorld->Simulate();
    scene.GameWorld->Start();
    agent.SetDestination( Vector3( 0.f, 0.f, 6.f ) );
    CHECK( agent.IsPathPending() );

    float closestToWallCenter = 100.f;
    for( int second = 0; second < 8 && !agent.HasArrived(); ++second )
    {
        for( int frame = 0; frame < 60; ++frame )
        {
            scene.Run( 1.f / 60.f );
            const Vector3 position = walker->GetComponent<Transform>().GetWorldPosition();
            if( std::abs( position.z ) < 0.5f )
            {
                closestToWallCenter = std::min( closestToWallCenter, std::abs( position.x ) );
            }
        }
    }
    CHECK( agent.HasArrived() );
    CHECK( agent.IsOnNavMesh() );
    const Vector3 end = walker->GetComponent<Transform>().GetWorldPosition();
    CHECK( end.z == doctest::Approx( 6.f ).epsilon( 0.05 ) );
    CHECK( std::abs( end.x ) < 0.3f );
    CHECK( closestToWallCenter > 5.5f );   // went round the end of the wall, never through it
    CHECK( agent.GetRemainingDistance() < 0.2f );
}


TEST_CASE( "Navigation: two agents swap places without passing through each other" )
{
    Scene scene;
    scene.Box( "Ground", Vector3( 0.f, -0.5f, 0.f ), Vector3( 20.f, 1.f, 20.f ) );
    NavMeshSurface& surface = scene.Surface();
    REQUIRE( scene.Navigation.Bake( surface, false ) );

    EntityHandle a = scene.Create( "A", Vector3( 0.f, 0.f, -4.f ) );
    EntityHandle b = scene.Create( "B", Vector3( 0.f, 0.f, 4.f ) );
    for( EntityHandle entity : { a, b } )
    {
        NavMeshAgent& agent = entity->AddComponent<NavMeshAgent>();
        agent.Radius = 0.4f;
        agent.Speed = 3.f;
    }
    scene.GameWorld->Simulate();
    scene.GameWorld->Start();
    a->GetComponent<NavMeshAgent>().SetDestination( Vector3( 0.f, 0.f, 4.f ) );
    b->GetComponent<NavMeshAgent>().SetDestination( Vector3( 0.f, 0.f, -4.f ) );

    float closest = 100.f;
    for( int frame = 0; frame < 60 * 8; ++frame )
    {
        scene.Run( 1.f / 60.f );
        const Vector3 pa = a->GetComponent<Transform>().GetWorldPosition();
        const Vector3 pb = b->GetComponent<Transform>().GetWorldPosition();
        closest = std::min( closest, ( pa - pb ).Length() );
    }
    CHECK( a->GetComponent<NavMeshAgent>().HasArrived() );
    CHECK( b->GetComponent<NavMeshAgent>().HasArrived() );
    CHECK( closest > 0.5f );   // radii 0.4 each: avoidance kept them (mostly) apart
}


TEST_CASE( "Navigation: an obstacle carves the navmesh while the game runs, and Stop restores it" )
{
    Scene scene;
    scene.Box( "Ground", Vector3( 0.f, -0.5f, 0.f ), Vector3( 24.f, 1.f, 24.f ) );
    NavMeshSurface& surface = scene.Surface();
    REQUIRE( scene.Navigation.Bake( surface, false ) );

    // A wide crate across the middle, not baked (obstacles are left out of bakes).
    EntityHandle crate = scene.Create( "Crate", Vector3( 0.f, 0.5f, 0.f ) );
    crate->AddComponent<BoxCollider>().Size = Vector3( 16.f, 1.f, 1.f );
    NavMeshObstacle& obstacle = crate->AddComponent<NavMeshObstacle>();
    obstacle.Size = Vector3( 16.f, 1.f, 1.f );
    obstacle.CarveDelay = 0.f;
    CHECK( scene.Navigation.GatherInput( surface ).Areas.size() == 12 );   // ground only

    std::vector<Vector3> corners;
    REQUIRE( scene.Navigation.FindPath( Vector3( 0.f, 0.f, -5.f ), Vector3( 0.f, 0.f, 5.f ), corners ) == NavPathStatus::Complete );
    CHECK( PathLength( corners ) == doctest::Approx( 10.f ).epsilon( 0.05 ) );   // straight through: not carved in edit mode

    scene.GameWorld->Simulate();
    scene.GameWorld->Start();
    scene.Run( 2.f / 60.f );
    scene.Navigation.WaitForBakes();
    CHECK( scene.Navigation.GetObstacleCount() == 1 );
    REQUIRE( scene.Navigation.FindPath( Vector3( 0.f, 0.f, -5.f ), Vector3( 0.f, 0.f, 5.f ), corners ) == NavPathStatus::Complete );
    CHECK( PathLength( corners ) > 17.f );   // around an end of the crate (grown by the agent radius)
    NavMeshHit hit;
    CHECK_FALSE( scene.Navigation.SamplePosition( Vector3( 0.f, 0.f, 0.f ), 0.2f, hit ) );

    // Moving it re-carves where it was and where it is.
    crate->GetComponent<Transform>().SetPosition( Vector3( 0.f, 0.5f, 6.f ) );
    scene.Run( 2.f / 60.f );
    scene.Navigation.WaitForBakes();
    CHECK( scene.Navigation.SamplePosition( Vector3( 0.f, 0.f, 0.f ), 0.2f, hit ) );
    CHECK_FALSE( scene.Navigation.SamplePosition( Vector3( 0.f, 0.f, 6.f ), 0.2f, hit ) );

    // Stopping restores the navmesh as baked.
    scene.GameWorld->Stop();
    CHECK( scene.Navigation.SamplePosition( Vector3( 0.f, 0.f, 6.f ), 0.2f, hit ) );
    CHECK( scene.Navigation.GetObstacleCount() == 0 );
}


TEST_CASE( "Navigation: a surface reports when its bake is out of date" )
{
    Scene scene;
    scene.Box( "Ground", Vector3( 0.f, -0.5f, 0.f ), Vector3( 24.f, 1.f, 24.f ) );
    NavMeshSurface& surface = scene.Surface();
    REQUIRE( scene.Navigation.Bake( surface, false ) );
    CHECK_FALSE( scene.Navigation.IsOutOfDate( surface, true ) );

    // New static geometry, then new settings, each make it stale until the next bake.
    EntityHandle wall = scene.Box( "New Wall", Vector3( 0.f, 1.f, 0.f ), Vector3( 6.f, 2.f, 1.f ) );
    scene.GameWorld->Simulate();
    CHECK( scene.Navigation.IsOutOfDate( surface, true ) );
    REQUIRE( scene.Navigation.Bake( surface, false ) );
    CHECK_FALSE( scene.Navigation.IsOutOfDate( surface, true ) );
    surface.AgentRadius = 0.6f;
    CHECK( scene.Navigation.IsOutOfDate( surface, true ) );
    REQUIRE( scene.Navigation.Bake( surface, false ) );
    CHECK_FALSE( scene.Navigation.IsOutOfDate( surface, true ) );
    // Dynamic things don't count.
    wall->AddComponent<Rigidbody>().Type = BodyType::Dynamic;
    scene.GameWorld->Simulate();
    CHECK( scene.Navigation.IsOutOfDate( surface, true ) );   // the wall left the static set
}
