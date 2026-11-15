#include <doctest/doctest.h>

#include "MeshBatchRegistry.h"
#include "RenderCommands.h"
#include "Camera/CameraData.h"
#include "Utils/CommandCache.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <map>
#include <random>
#include <set>
#include <tuple>

// ME-390: retained mesh commands. The batch registry keeps stable batch ids and membership by mesh
// command id; the mesh cache marks the ids that change; SameMeshCommand decides what a change is.

namespace RetainedMeshTest
{
    Moonlight::MeshBatchRegistry::Key MakeKey( uint64_t InMaterial, uint16_t InVertexBuffer, uint8_t InGroup = 0 )
    {
        Moonlight::MeshBatchRegistry::Key key;
        key.Material = InMaterial;
        key.VertexBuffer = InVertexBuffer;
        key.IndexBuffer = static_cast<uint16_t>( InVertexBuffer + 100 );
        key.Group = InGroup;
        return key;
    }
}


TEST_CASE( "Retained meshes: batches keep their ids while they have members" )
{
    using namespace RetainedMeshTest;
    Moonlight::MeshBatchRegistry registry;
    const uint32_t cubes = registry.Assign( 0, MakeKey( 0, 1 ) );
    const uint32_t spheres = registry.Assign( 1, MakeKey( 0, 2 ) );
    CHECK( cubes != spheres );
    CHECK( registry.Assign( 2, MakeKey( 0, 1 ) ) == cubes );
    CHECK( registry.GetMemberCount( cubes ) == 2 );
    CHECK( registry.GetBatchCount() == 2 );

    // Re-assigning a member to its own batch changes nothing.
    CHECK( registry.Assign( 0, MakeKey( 0, 1 ) ) == cubes );
    CHECK( registry.GetMemberCount( cubes ) == 2 );

    // Render group and material key separate batches too.
    CHECK( registry.Assign( 3, MakeKey( 0, 1, 1 ) ) != cubes );
    CHECK( registry.Assign( 4, MakeKey( 7, 1 ) ) != cubes );

    // A member that changes key moves; the batch it left keeps its id while others remain.
    CHECK( registry.Assign( 2, MakeKey( 0, 2 ) ) == spheres );
    CHECK( registry.GetMemberCount( cubes ) == 1 );
    CHECK( registry.GetMemberCount( spheres ) == 2 );
    CHECK( registry.GetBatch( 0 ) == cubes );

    // Emptied: its id is free, and the next new key reuses it.
    registry.Remove( 0 );
    CHECK( registry.GetBatch( 0 ) == Moonlight::MeshBatchRegistry::kNone );
    CHECK( registry.GetMemberCount( cubes ) == 0 );
    const uint32_t reused = registry.Assign( 5, MakeKey( 9, 9 ) );
    CHECK( reused == cubes );
    CHECK( registry.GetKey( reused ) == MakeKey( 9, 9 ) );
    // The old key makes a new batch rather than finding the reused id.
    CHECK( registry.Assign( 6, MakeKey( 0, 1 ) ) != reused );

    // Removing something that is in no batch is harmless.
    registry.Remove( 1000 );
    registry.Remove( 0 );
    CHECK( registry.GetBatch( 1000 ) == Moonlight::MeshBatchRegistry::kNone );
}


TEST_CASE( "Retained meshes: a hint skips the lookup only when it matches" )
{
    using namespace RetainedMeshTest;
    Moonlight::MeshBatchRegistry registry;
    const uint32_t a = registry.Assign( 0, MakeKey( 1, 1 ) );
    const uint32_t b = registry.Assign( 1, MakeKey( 2, 2 ) );
    CHECK( registry.Assign( 2, MakeKey( 1, 1 ), b ) == a );     // wrong hint: still the right batch
    CHECK( registry.Assign( 3, MakeKey( 2, 2 ), b ) == b );
    CHECK( registry.Assign( 4, MakeKey( 3, 3 ), 12345 ) != a ); // out of range hint
}


TEST_CASE( "Retained meshes: incremental changes match a registry rebuilt from scratch" )
{
    using namespace RetainedMeshTest;
    std::mt19937 random( 390 );
    Moonlight::MeshBatchRegistry registry;
    std::map<uint32_t, Moonlight::MeshBatchRegistry::Key> truth;
    for( int step = 0; step < 4000; ++step )
    {
        const uint32_t command = random() % 300;
        if( random() % 4 == 0 )
        {
            registry.Remove( command );
            truth.erase( command );
        }
        else
        {
            const auto key = MakeKey( random() % 3, static_cast<uint16_t>( random() % 5 ), static_cast<uint8_t>( random() % 2 ) );
            registry.Assign( command, key );
            truth[command] = key;
        }
    }
    // Every member is in a batch with its key; batches with the same key are one batch; counts add up.
    std::map<uint32_t, uint32_t> members;
    for( uint32_t command = 0; command < 300; ++command )
    {
        const uint32_t batch = registry.GetBatch( command );
        const auto expected = truth.find( command );
        if( expected == truth.end() )
        {
            CHECK( batch == Moonlight::MeshBatchRegistry::kNone );
            continue;
        }
        REQUIRE( batch != Moonlight::MeshBatchRegistry::kNone );
        CHECK( registry.GetKey( batch ) == expected->second );
        ++members[batch];
    }
    std::set<std::tuple<uint64_t, uint16_t, uint16_t, uint8_t>> keys;
    for( const auto& [batch, count] : members )
    {
        CHECK( registry.GetMemberCount( batch ) == count );
        const auto& key = registry.GetKey( batch );
        CHECK( keys.insert( { key.Material, key.VertexBuffer, key.IndexBuffer, key.Group } ).second );
    }
    CHECK( registry.GetBatchCount() == members.size() );
}


TEST_CASE( "Retained meshes: the mesh cache marks what changed" )
{
    CommandCache<Moonlight::MeshCommand> cache;
    cache.TrackChanges = true;
    Moonlight::MeshCommand command;
    const unsigned int a = cache.Push( command );
    const unsigned int b = cache.Push( command );
    REQUIRE( cache.Changed.size() == 2 );
    CHECK( cache.Changed[a] == 1 );
    CHECK( cache.Changed[b] == 1 );

    std::fill( cache.Changed.begin(), cache.Changed.end(), uint8_t( 0 ) );
    cache.AllChanged = false;
    command.BatchKey = 7;
    cache.Update( b, command );
    CHECK( cache.Changed[a] == 0 );
    CHECK( cache.Changed[b] == 1 );

    cache.Changed[b] = 0;
    cache.Pop( a );
    CHECK( cache.Changed[a] == 1 );
    // A freed id comes back marked.
    cache.Changed[a] = 0;
    CHECK( cache.Push( command ) == a );
    CHECK( cache.Changed[a] == 1 );

    // In-place edits mark through MarkChanged; ids past the end are ignored.
    cache.Changed[a] = 0;
    cache.MarkChanged( b );
    cache.MarkChanged( 99 );
    CHECK( cache.Changed[b] == 1 );

    cache.Clear();
    CHECK( cache.AllChanged );
    CHECK( cache.Changed.empty() );

    // Without tracking nothing is marked.
    CommandCache<Moonlight::MeshCommand> untracked;
    untracked.Push( command );
    untracked.Update( 0, command );
    CHECK( untracked.Changed.empty() );
}


TEST_CASE( "Retained meshes: Visible is not a change, everything a draw uses is" )
{
    Moonlight::MeshCommand a;
    a.Transform = glm::mat4( 1.f );
    a.ID = 4;
    a.BatchKey = 9;
    a.WorldBounds.Encapsulate( Vector3( 1.f, 2.f, 3.f ) );
    Moonlight::MeshCommand b = a;
    CHECK( Moonlight::SameMeshCommand( a, b ) );
    b.Visible = !a.Visible;
    CHECK( Moonlight::SameMeshCommand( a, b ) );
    b.CullDeferred = !a.CullDeferred;
    CHECK( Moonlight::SameMeshCommand( a, b ) );

    b = a;
    b.Transform[3][0] += 1.f;
    CHECK_FALSE( Moonlight::SameMeshCommand( a, b ) );
    b = a;
    b.BatchKey = 10;
    CHECK_FALSE( Moonlight::SameMeshCommand( a, b ) );
    b = a;
    b.CastShadows = !a.CastShadows;
    CHECK_FALSE( Moonlight::SameMeshCommand( a, b ) );
    b = a;
    b.InstanceData.x = 1.f;
    CHECK_FALSE( Moonlight::SameMeshCommand( a, b ) );
    b = a;
    b.WorldBounds.Encapsulate( Vector3( 5.f, 5.f, 5.f ) );
    CHECK_FALSE( Moonlight::SameMeshCommand( a, b ) );
    b = a;
    b.DitherFade = 0.5f;
    CHECK_FALSE( Moonlight::SameMeshCommand( a, b ) );
}


TEST_CASE( "Retained meshes: a cull left to the GPU path is tested when a camera reads it" )
{
    // ME-401: RenderCore skips the camera test for the GPU path's members and leaves their flags at
    // 0; whoever draws or picks one on the CPU tests its bounds against the camera's own frustum.
    Moonlight::CameraData camera;
    const glm::mat4 projection = glm::perspectiveLH( glm::radians( 60.f ), 16.f / 9.f, 0.1f, 100.f );
    const glm::mat4 view = glm::lookAtLH( glm::vec3( 0.f, 0.f, -10.f ), glm::vec3( 0.f ), glm::vec3( 0.f, 1.f, 0.f ) );
    camera.ViewFrustum.Update( projection * view );
    camera.VisibleFlags.assign( 4, 0 );

    Moonlight::MeshCommand inView;
    inView.WorldBounds = AABB( Vector3( -1.f, -1.f, -1.f ), Vector3( 1.f, 1.f, 1.f ) );
    inView.VisibilityIndex = 1;
    Moonlight::MeshCommand behind = inView;
    behind.WorldBounds = AABB( Vector3( -1.f, -1.f, -30.f ), Vector3( 1.f, 1.f, -28.f ) );
    REQUIRE( camera.ViewFrustum.Intersects( inView.WorldBounds ) );
    REQUIRE_FALSE( camera.ViewFrustum.Intersects( behind.WorldBounds ) );

    // Culled by RenderCore: the flag decides alone.
    CHECK_FALSE( Moonlight::CameraSeesMesh( camera, inView ) );
    camera.VisibleFlags[1] = 1;
    CHECK( Moonlight::CameraSeesMesh( camera, inView ) );
    CHECK( Moonlight::CameraSeesMesh( camera, behind ) );
    camera.VisibleFlags[1] = 0;

    // Left to the GPU path: the frustum decides.
    inView.CullDeferred = true;
    behind.CullDeferred = true;
    CHECK( Moonlight::CameraSeesMesh( camera, inView ) );
    CHECK_FALSE( Moonlight::CameraSeesMesh( camera, behind ) );

    // An id outside the camera's flags (a camera RenderCore didn't size) sees nothing.
    inView.VisibilityIndex = 9;
    CHECK_FALSE( Moonlight::CameraSeesMesh( camera, inView ) );
    inView.VisibilityIndex = -1;
    CHECK_FALSE( Moonlight::CameraSeesMesh( camera, inView ) );
}
