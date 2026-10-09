#include <doctest/doctest.h>
#include "Lighting/ClusterBuilder.h"
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>

namespace
{
    using Moonlight::ClusterBuilder;

    struct ClusterFixture
    {
        // Camera at the origin looking down +Z (left-handed, like the renderer's views).
        Matrix4 View = Matrix4( glm::mat4( 1.f ) );
        Matrix4 Projection = Matrix4( glm::perspectiveLH( glm::radians( 60.f ), 16.f / 9.f, 0.1f, 100.f ) );
        ClusterBuilder Builder;

        void Build( const std::vector<Moonlight::LightCommand>& InLights )
        {
            Builder.Build( InLights, View, Projection, 0.1f, 100.f, 1600.f, 900.f );
        }

        int Slice( float InViewZ ) const
        {
            const ClusterBuilder::Params& params = Builder.GetParams();
            return static_cast<int>( std::floor( std::log( InViewZ ) * params.SliceScale + params.SliceBias ) );
        }

        uint32_t Count( uint32_t InX, uint32_t InY, uint32_t InZ ) const
        {
            const uint32_t cluster = InX + InY * ClusterBuilder::kGridX + InZ * ClusterBuilder::kGridX * ClusterBuilder::kGridY;
            return static_cast<uint32_t>( Builder.GetGrid()[cluster * 4 + 1] );
        }

        uint32_t TotalAssignments() const
        {
            uint32_t total = 0;
            for( uint32_t cluster = 0; cluster < ClusterBuilder::kClusterCount; ++cluster )
            {
                total += static_cast<uint32_t>( Builder.GetGrid()[cluster * 4 + 1] );
            }
            return total;
        }
    };

    Moonlight::LightCommand PointLight( const Vector3& InPosition, float InRange )
    {
        Moonlight::LightCommand light;
        light.Type = Moonlight::LightType::Point;
        light.Position = InPosition;
        light.Range = InRange;
        return light;
    }
}

TEST_CASE( "ClusterBuilder: depth slices are exponential between near and far" )
{
    ClusterFixture fixture;
    fixture.Build( {} );
    CHECK( fixture.Slice( 0.1f ) == 0 );
    CHECK( fixture.Slice( 99.9f ) == static_cast<int>( ClusterBuilder::kGridZ ) - 1 );
    // Equal depth ratios span equal slice counts.
    CHECK( std::abs( ( fixture.Slice( 10.f ) - fixture.Slice( 1.f ) ) - ( fixture.Slice( 1.f ) - fixture.Slice( 0.1f ) ) ) <= 1 );
    CHECK( fixture.TotalAssignments() == 0 );
}

TEST_CASE( "ClusterBuilder: a small light lands only in the clusters around it" )
{
    ClusterFixture fixture;
    fixture.Build( { PointLight( Vector3( 0.f, 0.f, 10.f ), 0.5f ) } );

    const uint32_t slice = static_cast<uint32_t>( fixture.Slice( 10.f ) );
    // The screen centre falls between tiles 7 and 8 horizontally and in row 4 vertically.
    CHECK( fixture.Count( 7, 4, slice ) == 1 );
    CHECK( fixture.Count( 8, 4, slice ) == 1 );
    const uint32_t cluster = 7 + 4 * ClusterBuilder::kGridX + slice * ClusterBuilder::kGridX * ClusterBuilder::kGridY;
    const uint32_t offset = static_cast<uint32_t>( fixture.Builder.GetGrid()[cluster * 4] );
    CHECK( fixture.Builder.GetIndices()[offset] == 0.f );

    // Nothing in the screen corners or far away in depth.
    CHECK( fixture.Count( 0, 0, slice ) == 0 );
    CHECK( fixture.Count( 15, 8, slice ) == 0 );
    CHECK( fixture.Count( 7, 4, 0 ) == 0 );
    CHECK( fixture.Count( 7, 4, ClusterBuilder::kGridZ - 1 ) == 0 );
    CHECK( fixture.TotalAssignments() < 20 );
}

TEST_CASE( "ClusterBuilder: lights behind the camera or past the far plane are skipped" )
{
    ClusterFixture fixture;
    fixture.Build( { PointLight( Vector3( 0.f, 0.f, -5.f ), 1.f ), PointLight( Vector3( 0.f, 0.f, 150.f ), 10.f ), PointLight( Vector3( 80.f, 0.f, 10.f ), 1.f ) } );
    CHECK( fixture.TotalAssignments() == 0 );
}

TEST_CASE( "ClusterBuilder: a light around the camera covers every tile of the near slices" )
{
    ClusterFixture fixture;
    fixture.Build( { PointLight( Vector3( 0.f, 0.f, 0.f ), 2.f ) } );
    CHECK( fixture.Count( 0, 0, 0 ) == 1 );
    CHECK( fixture.Count( 15, 8, 0 ) == 1 );
    CHECK( fixture.Count( 15, 0, static_cast<uint32_t>( fixture.Slice( 1.5f ) ) ) == 1 );
    CHECK( fixture.Count( 0, 0, static_cast<uint32_t>( fixture.Slice( 3.f ) ) ) == 0 );
}

TEST_CASE( "ClusterBuilder: per-cluster lists cap at the maximum and offsets stay packed" )
{
    ClusterFixture fixture;
    std::vector<Moonlight::LightCommand> lights;
    for( int i = 0; i < 100; ++i )
    {
        lights.push_back( PointLight( Vector3( 0.f, 0.f, 10.f ), 1.f ) );
    }
    fixture.Build( lights );
    const uint32_t slice = static_cast<uint32_t>( fixture.Slice( 10.f ) );
    CHECK( fixture.Count( 7, 4, slice ) == ClusterBuilder::kMaxLightsPerCluster );

    // Offsets are a running sum of the counts.
    uint32_t expected = 0;
    bool packed = true;
    for( uint32_t cluster = 0; cluster < ClusterBuilder::kClusterCount; ++cluster )
    {
        packed = packed && static_cast<uint32_t>( fixture.Builder.GetGrid()[cluster * 4] ) == expected;
        expected += static_cast<uint32_t>( fixture.Builder.GetGrid()[cluster * 4 + 1] );
    }
    CHECK( packed );
    CHECK( fixture.Builder.GetUsedIndexRows() >= 1 );
}
