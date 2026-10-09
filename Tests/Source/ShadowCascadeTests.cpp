#include <doctest/doctest.h>
#include "Lighting/ShadowCascades.h"
#include <glm/glm.hpp>
#include <cmath>

namespace
{
    Moonlight::CascadeInput MakeInput()
    {
        Moonlight::CascadeInput input;
        input.CameraPosition = Vector3( 3.f, 5.f, -10.f );
        input.CameraFront = Vector3( 0.2f, -0.3f, 1.f ).Normalized();
        input.FovDegrees = 60.f;
        input.Aspect = 16.f / 9.f;
        input.Near = 0.1f;
        input.ShadowDistance = 80.f;
        input.LightDirection = Vector3( 0.4f, -1.f, 0.3f ).Normalized();
        input.Resolution = 1024;
        return input;
    }

    glm::vec3 Project( const glm::mat4& InViewProjection, const glm::vec3& InPoint )
    {
        const glm::vec4 clip = InViewProjection * glm::vec4( InPoint, 1.f );
        return glm::vec3( clip ) / clip.w;
    }
}

TEST_CASE( "ShadowCascades: splits increase and end at the shadow distance" )
{
    const Moonlight::CascadeInput input = MakeInput();
    Moonlight::CascadeSetup cascades[Moonlight::kMaxCascades];
    Moonlight::ComputeCascades( input, cascades );
    float previous = input.Near;
    for( const Moonlight::CascadeSetup& cascade : cascades )
    {
        CHECK( cascade.SplitNear == doctest::Approx( previous ) );
        CHECK( cascade.SplitFar > cascade.SplitNear );
        previous = cascade.SplitFar;
    }
    CHECK( cascades[3].SplitFar == doctest::Approx( 80.f ) );
    // Logarithmic-leaning: the first cascade is much shorter than the last.
    CHECK( cascades[0].SplitFar - cascades[0].SplitNear < ( cascades[3].SplitFar - cascades[3].SplitNear ) * 0.25f );
}

TEST_CASE( "ShadowCascades: every cascade contains its slice of the view frustum" )
{
    const Moonlight::CascadeInput input = MakeInput();
    Moonlight::CascadeSetup cascades[Moonlight::kMaxCascades];
    Moonlight::ComputeCascades( input, cascades );

    const glm::vec3 position = input.CameraPosition.InternalVector;
    const glm::vec3 front = glm::normalize( input.CameraFront.InternalVector );
    const glm::vec3 right = glm::normalize( glm::cross( glm::vec3( 0.f, 1.f, 0.f ), front ) );
    const glm::vec3 up = glm::cross( front, right );
    const float tanHalf = std::tan( glm::radians( input.FovDegrees ) * 0.5f );

    bool inside = true;
    for( const Moonlight::CascadeSetup& cascade : cascades )
    {
        for( float depth : { cascade.SplitNear, cascade.SplitFar } )
        {
            for( float sx : { -1.f, 1.f } )
            {
                for( float sy : { -1.f, 1.f } )
                {
                    const glm::vec3 corner = position + front * depth + right * ( sx * depth * tanHalf * input.Aspect ) + up * ( sy * depth * tanHalf );
                    const glm::vec3 ndc = Project( cascade.ViewProjection, corner );
                    inside = inside && std::abs( ndc.x ) <= 1.001f && std::abs( ndc.y ) <= 1.001f && ndc.z >= -0.001f && ndc.z <= 1.001f;
                }
            }
        }
    }
    CHECK( inside );
}

TEST_CASE( "ShadowCascades: cascades snap to whole texels as the camera moves" )
{
    Moonlight::CascadeInput input = MakeInput();
    for( float offset : { 0.f, 0.013f, 0.37f, 1.91f } )
    {
        input.CameraPosition = Vector3( 3.f + offset, 5.f, -10.f + offset * 0.5f );
        Moonlight::CascadeSetup cascades[Moonlight::kMaxCascades];
        Moonlight::ComputeCascades( input, cascades );
        for( const Moonlight::CascadeSetup& cascade : cascades )
        {
            const glm::vec3 origin = Project( cascade.ViewProjection, glm::vec3( 0.f ) ) * ( input.Resolution * 0.5f );
            CHECK( std::abs( origin.x - std::round( origin.x ) ) < 0.02f );
            CHECK( std::abs( origin.y - std::round( origin.y ) ) < 0.02f );
            CHECK( cascade.TexelWorldSize == doctest::Approx( cascade.Radius * 2.f / input.Resolution ) );
        }
    }
}

TEST_CASE( "ShadowCascades: casters between the light and the cascade stay inside its depth range" )
{
    Moonlight::CascadeInput input = MakeInput();
    // A tall caster far up towards the light from the first cascade.
    const glm::vec3 towardsLight = -glm::normalize( input.LightDirection.InternalVector ) * 60.f;
    const glm::vec3 casterPoint = input.CameraPosition.InternalVector + towardsLight;
    input.CasterBounds = AABB( Vector3( casterPoint.x - 1.f, casterPoint.y - 1.f, casterPoint.z - 1.f ), Vector3( casterPoint.x + 1.f, casterPoint.y + 1.f, casterPoint.z + 1.f ) );
    Moonlight::CascadeSetup cascades[Moonlight::kMaxCascades];
    Moonlight::ComputeCascades( input, cascades );
    for( const Moonlight::CascadeSetup& cascade : cascades )
    {
        const glm::vec3 ndc = Project( cascade.ViewProjection, casterPoint );
        CHECK( ndc.z >= -0.001f );
    }

    // Without caster bounds the same point would be clipped by the first cascade's near plane.
    input.CasterBounds = AABB();
    Moonlight::ComputeCascades( input, cascades );
    CHECK( Project( cascades[0].ViewProjection, casterPoint ).z < 0.f );
}
