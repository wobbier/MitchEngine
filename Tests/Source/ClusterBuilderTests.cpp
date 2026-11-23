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

TEST_CASE( "ClusterBuilder: extended emitters remain visible when their centre range is offscreen" )
{
    ClusterFixture fixture;
    Moonlight::LightCommand light = PointLight( Vector3( 15.f, 0.f, 10.f ), 0.5f );
    fixture.Build( { light } );
    REQUIRE( fixture.TotalAssignments() == 0 );

    // The segment reaches into the view even though its centre is outside it.
    light.Type = Moonlight::LightType::Bar;
    light.HalfSize = Vector3( 12.f, 0.1f, 0.1f );
    fixture.Build( { light } );
    CHECK( fixture.Count( 15, 4, static_cast<uint32_t>( fixture.Slice( 10.f ) ) ) == 1 );

    light.Type = Moonlight::LightType::Box;
    fixture.Build( { light } );
    CHECK( fixture.Count( 15, 4, static_cast<uint32_t>( fixture.Slice( 10.f ) ) ) == 1 );

    light.Type = Moonlight::LightType::Rectangle;
    light.HalfSize.z = 0.f;
    fixture.Build( { light } );
    CHECK( fixture.Count( 15, 4, static_cast<uint32_t>( fixture.Slice( 10.f ) ) ) == 1 );
}

TEST_CASE( "ClusterBuilder: a bar end can reach the near plane from behind the camera" )
{
    ClusterFixture fixture;
    Moonlight::LightCommand light = PointLight( Vector3( 0.f, 0.f, -5.f ), 0.5f );
    light.Type = Moonlight::LightType::Bar;
    light.AxisX = Vector3( 0.f, 0.f, 1.f );
    light.HalfSize = Vector3( 6.f, 0.1f, 0.1f );
    fixture.Build( { light } );
    CHECK( fixture.Count( 7, 4, 0 ) == 1 );
    CHECK( fixture.Count( 8, 4, static_cast<uint32_t>( fixture.Slice( 1.f ) ) ) == 1 );
}

namespace
{
    Moonlight::LightCommand SpotLight( const Vector3& InPosition, const Vector3& InDirection, float InRange, float InOuterDegrees )
    {
        Moonlight::LightCommand light = PointLight( InPosition, InRange );
        light.Type = Moonlight::LightType::Spot;
        light.Direction = InDirection;
        light.CosOuter = std::cos( glm::radians( InOuterDegrees ) );
        light.CosInner = std::min( 1.f, light.CosOuter + 0.05f );
        return light;
    }

    // Every point a spot lights (inside its outer cone, within Range) that the camera sees falls
    // in a cluster listing it. Returns how many such points were checked.
    int CheckSpotCoverage( ClusterFixture& InFixture, const Moonlight::LightCommand& InSpot )
    {
        const glm::vec3 apex = InSpot.Position.InternalVector;
        const glm::vec3 axis = glm::normalize( InSpot.Direction.InternalVector );
        const glm::vec3 side = glm::normalize( glm::cross( axis, std::abs( axis.y ) < 0.9f ? glm::vec3( 0.f, 1.f, 0.f ) : glm::vec3( 1.f, 0.f, 0.f ) ) );
        const glm::vec3 up = glm::cross( side, axis );
        const float outer = std::acos( InSpot.CosOuter ) * 0.999f;
        const glm::mat4 projection = InFixture.Projection.GetInternalMatrix();
        int checked = 0;
        for( int d = 1; d <= 12; ++d )
        {
            const float distance = InSpot.Range * d / 12.f * 0.999f;
            for( int a = 0; a <= 6; ++a )
            {
                const float angle = outer * a / 6.f;
                for( int r = 0; r < 12; ++r )
                {
                    const float turn = glm::two_pi<float>() * r / 12.f;
                    const glm::vec3 offAxis = side * std::cos( turn ) + up * std::sin( turn );
                    const glm::vec3 point = apex + distance * ( axis * std::cos( angle ) + offAxis * std::sin( angle ) );
                    if( point.z <= 0.15f || point.z >= 99.f )
                    {
                        continue;
                    }
                    const glm::vec4 clip = projection * glm::vec4( point, 1.f );
                    const float x = clip.x / clip.w, y = clip.y / clip.w;
                    if( std::abs( x ) >= 0.999f || std::abs( y ) >= 0.999f )
                    {
                        continue;
                    }
                    const uint32_t tileX = static_cast<uint32_t>( ( x * 0.5f + 0.5f ) * ClusterBuilder::kGridX );
                    const uint32_t tileY = static_cast<uint32_t>( ( 0.5f - y * 0.5f ) * ClusterBuilder::kGridY );
                    const uint32_t slice = static_cast<uint32_t>( std::clamp( InFixture.Slice( point.z ), 0, static_cast<int>( ClusterBuilder::kGridZ ) - 1 ) );
                    CHECK( InFixture.Count( tileX, tileY, slice ) == 1 );
                    ++checked;
                }
            }
        }
        return checked;
    }
}

TEST_CASE( "ClusterBuilder: a spot covers every cluster its cone lights, and a narrow one far fewer than its range sphere" )
{
    ClusterFixture fixture;
    const Vector3 apex( -4.f, 3.f, 12.f );
    const Vector3 across = Vector3( 1.f, -0.4f, 0.6f ).Normalized();
    for( float degrees : { 8.f, 30.f, 45.f, 60.f, 85.f, 120.f } )
    {
        CAPTURE( degrees );
        const Moonlight::LightCommand spot = SpotLight( apex, across, 14.f, degrees );
        fixture.Build( { spot } );
        CHECK( CheckSpotCoverage( fixture, spot ) > 100 );
    }

    // A follow spot's beam against the sphere the cluster used to take for it.
    fixture.Build( { SpotLight( apex, across, 14.f, 8.f ) } );
    const uint32_t narrow = fixture.TotalAssignments();
    fixture.Build( { PointLight( apex, 14.f ) } );
    const uint32_t sphere = fixture.TotalAssignments();
    CHECK( narrow * 4 < sphere );
}


namespace
{
    // The cluster a view-space point falls in, or false when the camera doesn't see it.
    bool ClusterOf( const ClusterFixture& InFixture, const glm::vec3& InPoint, uint32_t& OutX, uint32_t& OutY, uint32_t& OutZ )
    {
        if( InPoint.z <= 0.11f || InPoint.z >= 99.f )
        {
            return false;
        }
        const glm::vec4 clip = InFixture.Projection.GetInternalMatrix() * glm::vec4( InPoint, 1.f );
        const float x = clip.x / clip.w, y = clip.y / clip.w;
        if( std::abs( x ) >= 0.999f || std::abs( y ) >= 0.999f )
        {
            return false;
        }
        OutX = static_cast<uint32_t>( ( x * 0.5f + 0.5f ) * ClusterBuilder::kGridX );
        OutY = static_cast<uint32_t>( ( 0.5f - y * 0.5f ) * ClusterBuilder::kGridY );
        OutZ = static_cast<uint32_t>( std::clamp( InFixture.Slice( InPoint.z ), 0, static_cast<int>( ClusterBuilder::kGridZ ) - 1 ) );
        return true;
    }
}

TEST_CASE( "ClusterBuilder: a light beside the camera stays out of the far side's clusters (ME-408)" )
{
    ClusterFixture fixture;
    // Its range reaches past the near plane: it used to fill every tile of every slice it reached.
    fixture.Build( { PointLight( Vector3( -3.f, 0.f, 0.5f ), 4.f ) } );
    const uint32_t deep = static_cast<uint32_t>( fixture.Slice( 3.f ) );
    CHECK( fixture.Count( 0, 4, static_cast<uint32_t>( fixture.Slice( 1.f ) ) ) == 1 );
    CHECK( fixture.Count( 15, 4, deep ) == 0 );
    CHECK( fixture.Count( 15, 0, deep ) == 0 );

    // Near slices are narrow enough that it reaches every tile there; deeper ones only on its
    // side. Before, it filled the whole screen in all 14 slices it reaches (2016 clusters).
    uint32_t block = 0;
    for( int z = 0; z <= fixture.Slice( 4.5f ); ++z )
    {
        block += ClusterBuilder::kGridX * ClusterBuilder::kGridY;
    }
    CHECK( fixture.TotalAssignments() < block * 9 / 10 );
    uint32_t farRight = 0;
    for( uint32_t z = static_cast<uint32_t>( fixture.Slice( 2.f ) ); z < ClusterBuilder::kGridZ; ++z )
    {
        for( uint32_t y = 0; y < ClusterBuilder::kGridY; ++y )
        {
            for( uint32_t x = 12; x < ClusterBuilder::kGridX; ++x )
            {
                farRight += fixture.Count( x, y, z );
            }
        }
    }
    CHECK( farRight == 0 );
}

TEST_CASE( "ClusterBuilder: every point a light reaches lands in a cluster listing it" )
{
    // A ring of lights around and in front of the camera, close and far, inside and outside the
    // near plane; points sampled through each sphere must find their light in their cluster.
    ClusterFixture fixture;
    std::vector<Moonlight::LightCommand> lights;
    const glm::vec3 centres[] = { { 0.f, 0.f, 0.f }, { -3.f, 1.f, 0.5f }, { 4.f, -1.f, 2.f }, { 0.5f, 2.f, 6.f }, { -6.f, -2.f, 15.f }, { 8.f, 3.f, 30.f }, { 0.f, 0.f, 60.f } };
    const float ranges[] = { 1.5f, 4.f, 3.f, 2.5f, 6.f, 9.f, 20.f };
    for( int i = 0; i < 7; ++i )
    {
        lights.push_back( PointLight( Vector3( centres[i].x, centres[i].y, centres[i].z ), ranges[i] ) );
    }
    fixture.Build( lights );
    const std::vector<float>& grid = fixture.Builder.GetGrid();
    const std::vector<float>& indices = fixture.Builder.GetIndices();
    int checked = 0;
    for( int i = 0; i < 7; ++i )
    {
        CAPTURE( i );
        for( int d = 0; d <= 8; ++d )
        {
            const float distance = ranges[i] * d / 8.f * 0.995f;
            for( int t = 0; t < 10; ++t )
            {
                for( int p = 0; p < 20; ++p )
                {
                    const float theta = glm::pi<float>() * ( t + 0.5f ) / 10.f;
                    const float phi = glm::two_pi<float>() * p / 20.f;
                    const glm::vec3 point = centres[i] + distance * glm::vec3( std::sin( theta ) * std::cos( phi ), std::cos( theta ), std::sin( theta ) * std::sin( phi ) );
                    uint32_t x, y, z;
                    if( !ClusterOf( fixture, point, x, y, z ) )
                    {
                        continue;
                    }
                    const uint32_t cluster = x + y * ClusterBuilder::kGridX + z * ClusterBuilder::kGridX * ClusterBuilder::kGridY;
                    const uint32_t offset = static_cast<uint32_t>( grid[cluster * 4] );
                    const uint32_t count = static_cast<uint32_t>( grid[cluster * 4 + 1] );
                    bool listed = false;
                    for( uint32_t k = 0; k < count; ++k )
                    {
                        listed = listed || indices[offset + k] == static_cast<float>( i );
                    }
                    CHECK( listed );
                    ++checked;
                }
            }
        }
    }
    CHECK( checked > 2000 );
}
