#include "ShadowCascades.h"
#include <bx/math.h>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>

namespace Moonlight
{
    float CascadeSplitDistance( uint32_t InIndex, uint32_t InCount, float InNear, float InFar, float InLambda )
    {
        const float ratio = static_cast<float>( InIndex ) / static_cast<float>( std::max<uint32_t>( InCount, 1 ) );
        const float logarithmic = InNear * std::pow( InFar / InNear, ratio );
        const float uniform = InNear + ( InFar - InNear ) * ratio;
        return InLambda * logarithmic + ( 1.f - InLambda ) * uniform;
    }


    void ComputeCascades( const CascadeInput& InInput, CascadeSetup* OutCascades )
    {
        const uint32_t count = std::clamp<uint32_t>( InInput.CascadeCount, 1, kMaxCascades );
        const float nearPlane = std::max( InInput.Near, 0.01f );
        const float farPlane = std::max( InInput.ShadowDistance, nearPlane + 0.1f );

        const glm::vec3 position = InInput.CameraPosition.InternalVector;
        const glm::vec3 front = glm::normalize( InInput.CameraFront.InternalVector );
        glm::vec3 right = glm::cross( InInput.CameraUp.InternalVector, front );
        right = glm::length( right ) > 1e-5f ? glm::normalize( right ) : glm::vec3( 1.f, 0.f, 0.f );
        const glm::vec3 up = glm::cross( front, right );
        const float tanHalfFov = std::tan( glm::radians( InInput.FovDegrees ) * 0.5f );

        glm::vec3 lightDirection = InInput.LightDirection.InternalVector;
        lightDirection = glm::length( lightDirection ) > 1e-5f ? glm::normalize( lightDirection ) : glm::vec3( 0.f, -1.f, 0.f );
        const glm::vec3 lightUp = std::abs( lightDirection.y ) > 0.99f ? glm::vec3( 0.f, 0.f, 1.f ) : glm::vec3( 0.f, 1.f, 0.f );

        for( uint32_t i = 0; i < count; ++i )
        {
            CascadeSetup& cascade = OutCascades[i];
            cascade.SplitNear = CascadeSplitDistance( i, count, nearPlane, farPlane, InInput.SplitLambda );
            cascade.SplitFar = CascadeSplitDistance( i + 1, count, nearPlane, farPlane, InInput.SplitLambda );

            // Corners of this slice of the view frustum.
            glm::vec3 corners[8];
            int cornerIndex = 0;
            for( float depth : { cascade.SplitNear, cascade.SplitFar } )
            {
                const float halfHeight = InInput.Orthographic ? InInput.OrthographicSize : depth * tanHalfFov;
                const float halfWidth = halfHeight * InInput.Aspect;
                const glm::vec3 center = position + front * depth;
                for( float sy : { -1.f, 1.f } )
                {
                    for( float sx : { -1.f, 1.f } )
                    {
                        corners[cornerIndex++] = center + right * ( sx * halfWidth ) + up * ( sy * halfHeight );
                    }
                }
            }

            // Bounding sphere (rotation invariant), radius rounded so it is stable frame to frame.
            glm::vec3 center( 0.f );
            for( const glm::vec3& corner : corners )
            {
                center += corner;
            }
            center /= 8.f;
            float radius = 0.f;
            for( const glm::vec3& corner : corners )
            {
                radius = std::max( radius, glm::length( corner - center ) );
            }
            radius = std::ceil( radius * 16.f ) / 16.f;

            // Pull the near plane back to include every caster between the light and this cascade.
            float extension = 0.f;
            if( InInput.CasterBounds.IsValid() )
            {
                const AABB& bounds = InInput.CasterBounds;
                for( int c = 0; c < 8; ++c )
                {
                    const glm::vec3 corner( ( c & 1 ) ? bounds.Max.x : bounds.Min.x, ( c & 2 ) ? bounds.Max.y : bounds.Min.y, ( c & 4 ) ? bounds.Max.z : bounds.Min.z );
                    extension = std::max( extension, glm::dot( center - corner, lightDirection ) - radius );
                }
                extension = std::min( extension, 2000.f );
            }

            const glm::vec3 eye = center - lightDirection * ( radius + extension );
            cascade.View = glm::lookAtLH( eye, center, lightUp );
            bx::mtxOrtho( &cascade.Projection[0][0], -radius, radius, -radius, radius, 0.f, radius * 2.f + extension, 0.f, InInput.HomogeneousDepth );

            // Snap the world origin to a texel so the cascade only ever moves in whole texels.
            const float resolution = static_cast<float>( std::max<uint32_t>( InInput.Resolution, 1 ) );
            glm::mat4 viewProjection = cascade.Projection * cascade.View;
            const glm::vec4 origin = viewProjection * glm::vec4( 0.f, 0.f, 0.f, 1.f );
            const glm::vec2 texelOrigin = glm::vec2( origin.x, origin.y ) * ( resolution * 0.5f );
            const glm::vec2 rounded( std::round( texelOrigin.x ), std::round( texelOrigin.y ) );
            const glm::vec2 offset = ( rounded - texelOrigin ) * ( 2.f / resolution );
            cascade.Projection[3][0] += offset.x;
            cascade.Projection[3][1] += offset.y;

            cascade.ViewProjection = cascade.Projection * cascade.View;
            cascade.TexelWorldSize = radius * 2.f / resolution;
            cascade.Center = center;
            cascade.Radius = radius;
        }
    }
}
