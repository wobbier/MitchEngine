#include <doctest/doctest.h>

#include "Culling/GpuMeshCulling.h"
#include "Path.h"
#include <algorithm>
#include <fstream>
#include <random>
#include <set>
#include <sstream>

// ME-389: GPU-driven culling. The CPU keeps the membership, the records and the layout the compute
// passes read; SimulateGpuCull runs those passes' arithmetic on the CPU, so it can be checked against
// a plain cull here (the shaders do the same steps, GpuCull.sh).

namespace GpuCullTest
{
    Moonlight::MeshBatchRegistry::Key MakeKey( uint64_t InMaterial, uint16_t InVertexBuffer = 1 )
    {
        Moonlight::MeshBatchRegistry::Key key;
        key.Material = InMaterial;
        key.VertexBuffer = InVertexBuffer;
        key.IndexBuffer = static_cast<uint16_t>( InVertexBuffer + 100 );
        return key;
    }

    AABB UnitBox( float InX )
    {
        return AABB( Vector3( InX, 0.f, 0.f ), Vector3( InX + 1.f, 1.f, 1.f ) );
    }
}


TEST_CASE( "GPU culling: a set lays out its batches by lowest member, each padded to whole groups" )
{
    using namespace GpuCullTest;
    Moonlight::MeshBatchRegistry registry;
    // Batch A gets ids 5, 9 and 300 of them; batch B gets 2 and 7; batch C is 600 members from 1000.
    const uint32_t a = registry.Assign( 9, MakeKey( 1 ) );
    registry.Assign( 5, MakeKey( 1 ) );
    const uint32_t b = registry.Assign( 7, MakeKey( 2 ) );
    registry.Assign( 2, MakeKey( 2 ) );
    for( uint32_t id = 10; id < 310; ++id )
    {
        registry.Assign( id, MakeKey( 1 ) );
    }
    const uint32_t c = registry.Assign( 1000, MakeKey( 3 ) );
    for( uint32_t id = 1001; id < 1600; ++id )
    {
        registry.Assign( id, MakeKey( 3 ) );
    }

    std::vector<uint32_t> members( 17, 42u );    // something before: the set starts after it
    std::vector<uint32_t> groupBatch( 3, 0u );
    Moonlight::GpuCullSetLayout set;
    Moonlight::BuildGpuCullSet( registry, 4, set, members, groupBatch );
    CHECK( set.MemberBase == 17 );
    CHECK( set.GroupBase == 3 );
    CHECK( set.BatchBase == 4 );
    REQUIRE( set.GetBatchCount() == 3 );
    // Ordered by lowest member: B (2), A (5), C (1000).
    CHECK( set.Batches[0].Batch == b );
    CHECK( set.Batches[0].Representative == 2 );
    CHECK( set.Batches[1].Batch == a );
    CHECK( set.Batches[1].Representative == 5 );
    CHECK( set.Batches[2].Batch == c );
    CHECK( set.Batches[0].MemberCount == 2 );
    CHECK( set.Batches[1].MemberCount == 302 );
    CHECK( set.Batches[2].MemberCount == 600 );
    // Whole groups each.
    CHECK( set.Batches[0].GroupCount == 1 );
    CHECK( set.Batches[1].GroupCount == 2 );
    CHECK( set.Batches[2].GroupCount == 3 );
    CHECK( set.Batches[1].MemberStart == Moonlight::kGpuCullGroup );
    CHECK( set.Batches[2].MemberStart == 3 * Moonlight::kGpuCullGroup );
    CHECK( set.Batches[2].GroupStart == 3 );
    CHECK( set.Groups == 6 );
    CHECK( set.Padded == 6 * Moonlight::kGpuCullGroup );
    REQUIRE( members.size() == 17 + set.Padded );
    REQUIRE( groupBatch.size() == 3 + set.Groups );
    // Members ascending inside a batch, then padding.
    CHECK( members[17] == 2 );
    CHECK( members[18] == 7 );
    CHECK( members[19] == Moonlight::kGpuCullNoMember );
    CHECK( members[17 + Moonlight::kGpuCullGroup] == 5 );
    CHECK( members[17 + Moonlight::kGpuCullGroup + 1] == 9 );
    CHECK( members[17 + Moonlight::kGpuCullGroup + 2] == 10 );
    CHECK( members[17 + Moonlight::kGpuCullGroup + 301] == 309 );
    CHECK( members[17 + Moonlight::kGpuCullGroup + 302] == Moonlight::kGpuCullNoMember );
    // Each group names its batch's index in the set.
    CHECK( groupBatch[3] == 0 );
    CHECK( groupBatch[4] == 1 );
    CHECK( groupBatch[5] == 1 );
    CHECK( groupBatch[6] == 2 );
    CHECK( groupBatch[8] == 2 );
}


TEST_CASE( "GPU culling: the three passes give each batch its visible members, in member order" )
{
    using namespace GpuCullTest;
    std::mt19937 random( 389 );
    for( int round = 0; round < 6; ++round )
    {
        Moonlight::MeshBatchRegistry registry;
        const uint32_t ids = 200 + random() % 3000;
        for( uint32_t id = 0; id < ids; ++id )
        {
            if( random() % 5 != 0 )
            {
                registry.Assign( id, MakeKey( random() % 7 ) );
            }
        }
        std::vector<uint32_t> members;
        std::vector<uint32_t> groupBatch;
        Moonlight::GpuCullSetLayout set;
        Moonlight::BuildGpuCullSet( registry, 0, set, members, groupBatch );

        const uint32_t views = round % 2 == 0 ? 1 : 4;
        const bool reverse = round % 3 == 1;
        std::vector<uint8_t> visible( static_cast<size_t>( ids ) * views );
        for( uint8_t& flag : visible )
        {
            flag = random() % 3 != 0 ? 1 : 0;
        }
        const auto isVisible = [&]( uint32_t InId, uint32_t InView ) { return visible[static_cast<size_t>( InId ) * views + InView] != 0; };
        const std::vector<std::vector<uint32_t>> culled = Moonlight::SimulateGpuCull( set, members, views, reverse, isVisible );
        REQUIRE( culled.size() == static_cast<size_t>( views ) * set.GetBatchCount() );

        for( uint32_t v = 0; v < views; ++v )
        {
            for( uint32_t b = 0; b < set.GetBatchCount(); ++b )
            {
                // What the CPU path would batch: the visible members, ascending (backwards under a prepass).
                std::vector<uint32_t> expected;
                for( uint32_t id = 0; id < ids; ++id )
                {
                    if( registry.GetBatch( id ) == set.Batches[b].Batch && isVisible( id, v ) )
                    {
                        expected.push_back( id );
                    }
                }
                if( reverse )
                {
                    std::reverse( expected.begin(), expected.end() );
                }
                CHECK( culled[v * set.GetBatchCount() + b] == expected );
                CHECK( expected.size() <= set.Batches[b].MemberCount );
            }
        }
    }
}


TEST_CASE( "GPU culling: a batch with more groups than one scan chunk keeps member order" )
{
    using namespace GpuCullTest;
    // 256 * 300 + 57 members in one batch: 301 groups (two offsets chunks, the second one partial), and a
    // non-power-of-two member count so the last group is padded.
    const uint32_t ids = 256 * 300 + 57;
    Moonlight::MeshBatchRegistry registry;
    for( uint32_t id = 0; id < ids; ++id )
    {
        registry.Assign( id, MakeKey( 0 ) );
    }
    std::vector<uint32_t> members;
    std::vector<uint32_t> groupBatch;
    Moonlight::GpuCullSetLayout set;
    Moonlight::BuildGpuCullSet( registry, 0, set, members, groupBatch );
    REQUIRE( set.GetBatchCount() == 1 );
    REQUIRE( set.Groups == 301 );

    std::mt19937 random( 402 );
    const uint32_t views = 3;
    std::vector<uint8_t> visible( static_cast<size_t>( ids ) * views );
    for( uint32_t id = 0; id < ids; ++id )
    {
        for( uint32_t v = 0; v < views; ++v )
        {
            // View 0 sees nearly all, view 1 about half, view 2 nothing but the last member.
            const uint32_t chance = v == 0 ? 9 : 2;
            visible[static_cast<size_t>( id ) * views + v] = v == 2 ? ( id == ids - 1 ? 1 : 0 ) : ( random() % 10 < chance ? 1 : 0 );
        }
    }
    const auto isVisible = [&]( uint32_t InId, uint32_t InView ) { return visible[static_cast<size_t>( InId ) * views + InView] != 0; };
    for( const bool reverse : { false, true } )
    {
        const std::vector<std::vector<uint32_t>> culled = Moonlight::SimulateGpuCull( set, members, views, reverse, isVisible );
        REQUIRE( culled.size() == views );
        for( uint32_t v = 0; v < views; ++v )
        {
            std::vector<uint32_t> expected;
            for( uint32_t id = 0; id < ids; ++id )
            {
                if( isVisible( id, v ) )
                {
                    expected.push_back( id );
                }
            }
            if( reverse )
            {
                std::reverse( expected.begin(), expected.end() );
            }
            REQUIRE( culled[v] == expected );
        }
    }
}


TEST_CASE( "GPU culling: draw arguments and instance regions never overlap" )
{
    using namespace GpuCullTest;
    Moonlight::MeshBatchRegistry registry;
    for( uint32_t id = 0; id < 900; ++id )
    {
        registry.Assign( id, MakeKey( id % 5, static_cast<uint16_t>( id % 3 ) ) );
    }
    std::vector<uint32_t> members;
    std::vector<uint32_t> groupBatch;
    Moonlight::GpuCullSetLayout set;
    Moonlight::BuildGpuCullSet( registry, 0, set, members, groupBatch );
    REQUIRE( set.GetBatchCount() == 15 );

    // Two culls back to back in one frame's buffers: a camera (1 view), then cascades (4 views).
    const uint32_t batches = set.GetBatchCount();
    const uint32_t firstArgs = 0;
    const uint32_t firstOut = 0;
    const uint32_t secondArgs = batches * 1;
    const uint32_t secondOut = set.Padded * 1;
    std::set<uint32_t> args;
    std::vector<std::pair<uint32_t, uint32_t>> ranges;
    const auto collect = [&]( uint32_t InArgsBase, uint32_t InOutBase, uint32_t InViews ) {
        for( uint32_t v = 0; v < InViews; ++v )
        {
            for( uint32_t b = 0; b < batches; ++b )
            {
                const uint32_t index = Moonlight::GpuCullArgsIndex( InArgsBase, batches, v, b );
                CHECK( args.insert( index ).second );
                const uint32_t start = Moonlight::GpuCullInstanceStart( InOutBase, set.Padded, v, set.Batches[b] );
                ranges.push_back( { start, start + set.Batches[b].MemberCount } );
                CHECK( start + set.Batches[b].MemberCount <= InOutBase + ( v + 1 ) * set.Padded );
            }
        }
    };
    collect( firstArgs, firstOut, 1 );
    collect( secondArgs, secondOut, 4 );
    CHECK( args.size() == batches * 5 );
    CHECK( *args.rbegin() == batches * 5 - 1 );
    std::sort( ranges.begin(), ranges.end() );
    for( size_t i = 1; i < ranges.size(); ++i )
    {
        CHECK( ranges[i - 1].second <= ranges[i].first );
    }
    CHECK( ranges.back().second <= set.Padded * 5 );
}


TEST_CASE( "GPU culling: a frame's changes update membership, records and the rows that go up" )
{
    using namespace GpuCullTest;
    using Set = Moonlight::GpuCullState::Set;
    Moonlight::GpuCullState state;
    state.SetMinBatchMembers( 1 );
    const auto opaque = MakeKey( 1 );
    const auto other = MakeKey( 2 );
    const auto shadow = MakeKey( 0 );
    const auto indexCount = []( uint32_t ) { return 36u; };

    // First frame: everything, as a full update (no rows; the whole buffer goes up).
    state.Begin( 8, true );
    for( uint32_t id = 0; id < 8; ++id )
    {
        const bool inOpaque = id != 6;
        state.SetMesh( id, inOpaque ? &opaque : nullptr, id % 2 == 0 ? &shadow : nullptr, glm::mat4( 1.f ), UnitBox( static_cast<float>( id ) ) );
    }
    CHECK( state.IsFullUpdate() );
    CHECK( state.Finish( indexCount ) );
    CHECK( state.GetStagingRows() == 0 );
    CHECK( state.GetMemberCount( Set::Opaque ) == 7 );
    CHECK( state.GetMemberCount( Set::Shadow ) == 4 );
    CHECK( state.GetLayout( Set::Opaque ).Batches[0].IndexCount == 36 );
    CHECK( state.GetLayout( Set::Shadow ).BatchBase == 1 );
    CHECK( state.GetOpaqueMask()[6] == 0 );
    CHECK( state.GetOpaqueMask()[7] == 1 );
    CHECK( state.GetOpaqueLeft().empty() );
    // Records: the matrix, then the bounds with w = 1 on the min.
    CHECK( state.GetRecords()[3 * Moonlight::kGpuCullRecordVec4s + 4] == glm::vec4( 3.f, 0.f, 0.f, 1.f ) );

    // A static frame: nothing changes, nothing goes up, the layout stays.
    state.Begin( 8, false );
    CHECK_FALSE( state.Finish( indexCount ) );
    CHECK( state.GetStagingRows() == 0 );

    // A mesh moves (same batch): one row, no new layout.
    state.Begin( 8, false );
    glm::mat4 moved( 1.f );
    moved[3] = glm::vec4( 10.f, 0.f, 0.f, 1.f );
    state.SetMesh( 2, &opaque, &shadow, moved, UnitBox( 10.f ) );
    CHECK_FALSE( state.Finish( indexCount ) );
    REQUIRE( state.GetStagingRows() == 1 );
    CHECK( state.GetStaging()[0].x == 2.f );
    CHECK( state.GetStaging()[1 + 3] == glm::vec4( 10.f, 0.f, 0.f, 1.f ) );
    CHECK( state.GetStaging()[1 + 4] == glm::vec4( 10.f, 0.f, 0.f, 1.f ) );

    // One changes batch, one leaves the opaque set but still casts, one leaves both, one is added.
    state.Begin( 9, false );
    state.SetMesh( 1, &other, nullptr, glm::mat4( 1.f ), UnitBox( 1.f ) );
    state.SetMesh( 4, nullptr, &shadow, glm::mat4( 1.f ), UnitBox( 4.f ) );
    state.SetMesh( 5, nullptr, nullptr, glm::mat4( 1.f ), AABB() );
    state.SetMesh( 8, &opaque, nullptr, glm::mat4( 1.f ), UnitBox( 8.f ) );
    CHECK( state.Finish( indexCount ) );
    CHECK( state.GetStagingRows() == 3 );    // 1, 4 and 8: 5 has no record any more
    CHECK( state.GetOpaqueLeft() == std::vector<uint32_t>{ 4, 5 } );
    CHECK( state.GetMemberCount( Set::Opaque ) == 6 );
    CHECK( state.GetMemberCount( Set::Shadow ) == 4 );
    REQUIRE( state.GetLayout( Set::Opaque ).GetBatchCount() == 2 );
    CHECK( state.GetLayout( Set::Opaque ).Batches[0].Representative == 0 );
    CHECK( state.GetLayout( Set::Opaque ).Batches[1].Representative == 1 );

    // A full update after that (the cache was cleared, or the path came back on): who was in the
    // opaque set before and isn't now counts as having left.
    state.Begin( 9, true );
    for( uint32_t id = 0; id < 9; ++id )
    {
        state.SetMesh( id, id < 3 ? &opaque : nullptr, nullptr, glm::mat4( 1.f ), UnitBox( 0.f ) );
    }
    CHECK( state.Finish( indexCount ) );
    CHECK( state.GetOpaqueLeft() == std::vector<uint32_t>{ 3, 7, 8 } );
    CHECK( state.GetMemberCount( Set::Shadow ) == 0 );
    CHECK( state.GetLayout( Set::Shadow ).GetBatchCount() == 0 );

    state.Reset();
    CHECK( state.IsEmpty() );
    state.Begin( 4, false );   // after a reset the next update is a full one whatever it asks
    CHECK( state.IsFullUpdate() );
}


TEST_CASE( "GPU culling: small batches stay on the CPU path, and each batch knows its bounds" )
{
    using namespace GpuCullTest;
    using Set = Moonlight::GpuCullState::Set;
    Moonlight::GpuCullState state;
    state.SetMinBatchMembers( 4 );
    const auto big = MakeKey( 1 );
    const auto small = MakeKey( 2 );
    state.Begin( 7, true );
    for( uint32_t id = 0; id < 7; ++id )
    {
        state.SetMesh( id, id < 5 ? &big : &small, nullptr, glm::mat4( 1.f ), UnitBox( static_cast<float>( id ) ) );
    }
    CHECK( state.Finish( nullptr ) );
    REQUIRE( state.GetLayout( Set::Opaque ).GetBatchCount() == 1 );
    CHECK( state.GetMemberCount( Set::Opaque ) == 5 );
    CHECK( state.GetOpaqueMask()[4] == 1 );
    CHECK( state.GetOpaqueMask()[5] == 0 );     // the two-member batch: the CPU path draws it
    const AABB& bounds = state.GetLayout( Set::Opaque ).Batches[0].Bounds;
    CHECK( bounds.Min.InternalVector == glm::vec3( 0.f ) );
    CHECK( bounds.Max.InternalVector == glm::vec3( 5.f, 1.f, 1.f ) );

    // A member moves: the bounds follow, the layout doesn't change.
    state.Begin( 7, false );
    state.SetMesh( 2, &big, nullptr, glm::mat4( 1.f ), UnitBox( 20.f ) );
    CHECK_FALSE( state.Finish( nullptr ) );
    CHECK( state.GetLayout( Set::Opaque ).Batches[0].Bounds.Max.x == 21.f );

    // The batch drops below the minimum: its members leave the GPU path.
    state.Begin( 7, false );
    state.SetMesh( 0, &small, nullptr, glm::mat4( 1.f ), UnitBox( 0.f ) );
    state.SetMesh( 1, &small, nullptr, glm::mat4( 1.f ), UnitBox( 1.f ) );
    CHECK( state.Finish( nullptr ) );
    // Now 3 big and 4 small: the small batch is laid out, the big one isn't.
    REQUIRE( state.GetLayout( Set::Opaque ).GetBatchCount() == 1 );
    CHECK( state.GetLayout( Set::Opaque ).Batches[0].Representative == 0 );
    CHECK( state.GetOpaqueLeft() == std::vector<uint32_t>{ 2, 3, 4 } );
    CHECK( state.GetOpaqueMask()[5] == 1 );
}


TEST_CASE( "GPU culling: records go up whole only when they must" )
{
    // First frame or everything changed: whole.
    CHECK( Moonlight::PlanGpuCullUpload( 0, true, 100, 200 ).Full );
    // A few rows: the scatter pass.
    Moonlight::GpuCullUploadPlan plan = Moonlight::PlanGpuCullUpload( 10, false, 1000, 1200 );
    CHECK_FALSE( plan.Full );
    CHECK( plan.Capacity == 1200 );
    // A quarter or more changed: whole (one copy beats a scatter of most of it).
    CHECK( Moonlight::PlanGpuCullUpload( 250, false, 1000, 1200 ).Full );
    // More commands than the buffer holds: it grows by half again, and goes up whole.
    plan = Moonlight::PlanGpuCullUpload( 3, false, 1300, 1200 );
    CHECK( plan.Full );
    CHECK( plan.Capacity == 1950 );
}


TEST_CASE( "GPU culling: the shaders' constants match the CPU's" )
{
    std::ifstream file( Path( "Assets/Shaders/Culling/GpuCull.sh" ).FullPath );
    REQUIRE( file.good() );
    std::stringstream text;
    text << file.rdbuf();
    const std::string source = text.str();
    CHECK( source.find( "#define CULL_GROUP " + std::to_string( Moonlight::kGpuCullGroup ) + "u" ) != std::string::npos );
    CHECK( source.find( "#define CULL_MAX_VIEWS " + std::to_string( Moonlight::kGpuCullMaxViews ) + "u" ) != std::string::npos );
    CHECK( source.find( "#define CULL_RECORD_VEC4S " + std::to_string( Moonlight::kGpuCullRecordVec4s ) + "u" ) != std::string::npos );
    CHECK( source.find( "uniform vec4 u_gpuCull[3 + 6 * " + std::to_string( Moonlight::kGpuCullMaxViews ) + "];" ) != std::string::npos );
}
