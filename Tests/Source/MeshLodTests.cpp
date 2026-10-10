#include <doctest/doctest.h>

#include "Graphics/MeshLod.h"
#include <cmath>
#include <vector>

namespace MeshLodTest
{
    // A UV sphere with a duplicated seam column (like an imported mesh with texture coordinates).
    void MakeSphere( int InSegments, int InRings, std::vector<float>& OutPositions, std::vector<uint32_t>& OutIndices )
    {
        const float pi = 3.14159265f;
        for( int ring = 0; ring <= InRings; ++ring )
        {
            const float theta = pi * static_cast<float>( ring ) / static_cast<float>( InRings );
            for( int segment = 0; segment <= InSegments; ++segment )
            {
                const float phi = 2.f * pi * static_cast<float>( segment ) / static_cast<float>( InSegments );
                OutPositions.push_back( std::sin( theta ) * std::cos( phi ) );
                OutPositions.push_back( std::cos( theta ) );
                OutPositions.push_back( std::sin( theta ) * std::sin( phi ) );
            }
        }
        const uint32_t stride = static_cast<uint32_t>( InSegments + 1 );
        for( int ring = 0; ring < InRings; ++ring )
        {
            for( int segment = 0; segment < InSegments; ++segment )
            {
                const uint32_t a = static_cast<uint32_t>( ring ) * stride + static_cast<uint32_t>( segment );
                const uint32_t b = a + stride;
                OutIndices.insert( OutIndices.end(), { a, b, a + 1, a + 1, b, b + 1 } );
            }
        }
    }
}

TEST_CASE( "Mesh LODs: each level is a coarser index list over the same vertices" )
{
    std::vector<float> positions;
    std::vector<uint32_t> indices;
    MeshLodTest::MakeSphere( 64, 32, positions, indices );
    const size_t vertexCount = positions.size() / 3;

    Moonlight::MeshLodSettings settings;
    settings.Levels = 3;
    settings.Reduction = 0.5f;
    settings.TransitionHeight = 0.4f;
    settings.MaxError = 0.05f;
    const std::vector<Moonlight::MeshLod> lods = Moonlight::BuildMeshLods( positions.data(), vertexCount, sizeof( float ) * 3, indices, settings );
    REQUIRE( lods.size() >= 2 );

    size_t previous = indices.size();
    float height = settings.TransitionHeight;
    for( const Moonlight::MeshLod& lod : lods )
    {
        CHECK( lod.Indices.size() % 3 == 0 );
        CHECK( lod.Indices.size() <= previous * 7 / 10 );
        CHECK( lod.Indices.size() >= 3 );
        for( uint32_t index : lod.Indices )
        {
            REQUIRE( index < vertexCount );
        }
        CHECK( lod.ScreenHeight == doctest::Approx( height ) );
        previous = lod.Indices.size();
        height *= 0.5f;
    }

    // Small meshes, and no levels asked for, keep full detail only.
    settings.MinTriangles = static_cast<uint32_t>( indices.size() / 3 + 1 );
    CHECK( Moonlight::BuildMeshLods( positions.data(), vertexCount, sizeof( float ) * 3, indices, settings ).empty() );
    settings.MinTriangles = 0;
    settings.Levels = 0;
    CHECK( Moonlight::BuildMeshLods( positions.data(), vertexCount, sizeof( float ) * 3, indices, settings ).empty() );
}

TEST_CASE( "Mesh LODs: levels switch by relative screen height" )
{
    // 90 degree vertical field of view: a unit sphere 10 m away covers a tenth of the view.
    CHECK( Moonlight::RelativeScreenHeight( 1.f, 10.f, 90.f, false, 0.f ) == doctest::Approx( 0.1f ) );
    // Orthographic: the sphere's diameter over the view's height (twice the half height).
    CHECK( Moonlight::RelativeScreenHeight( 1.f, 100.f, 90.f, true, 5.f ) == doctest::Approx( 0.2f ) );

    const float heights[] = { 0.3f, 0.15f };
    CHECK( Moonlight::SelectLod( 0.5f, heights, 2 ) == 0 );
    CHECK( Moonlight::SelectLod( 0.2f, heights, 2 ) == 1 );
    CHECK( Moonlight::SelectLod( 0.1f, heights, 2 ) == 2 );
    CHECK( Moonlight::SelectLod( 0.2f, heights, 2, 2.f ) == 0 );   // bias keeps detail longer
    CHECK( Moonlight::SelectLod( 5.f, heights, 2, 0.f ) == 2 );    // bias 0: always the coarsest
    CHECK( Moonlight::SelectLod( 0.01f, heights, 0 ) == 0 );       // no levels
}

TEST_CASE( "Mesh LODs: flat-shaded meshes (no shared vertices) still simplify" )
{
    std::vector<float> welded;
    std::vector<uint32_t> weldedIndices;
    MeshLodTest::MakeSphere( 48, 24, welded, weldedIndices );
    // Unweld: every triangle gets its own three vertices, as flat-shaded imports do.
    std::vector<float> positions;
    std::vector<uint32_t> indices;
    for( uint32_t index : weldedIndices )
    {
        indices.push_back( static_cast<uint32_t>( positions.size() / 3 ) );
        positions.insert( positions.end(), welded.begin() + index * 3, welded.begin() + index * 3 + 3 );
    }
    Moonlight::MeshLodSettings settings;
    settings.Levels = 2;
    settings.MaxError = 0.05f;
    const std::vector<Moonlight::MeshLod> lods = Moonlight::BuildMeshLods( positions.data(), positions.size() / 3, sizeof( float ) * 3, indices, settings );
    REQUIRE( !lods.empty() );
    CHECK( lods[0].Indices.size() <= indices.size() * 7 / 10 );
}
