#pragma once
#include "Math/Vector3.h"
#include <array>
#include <vector>
#include <algorithm>
#include <cmath>
#include <glm/mat4x4.hpp>
#include <glm/geometric.hpp>
#include "Graphics/ShaderStructures.h"

namespace Moonlight
{
    // Immutable, frame-major positions shared by all pieces/instances of a baked animation.
    struct VertexAnimation
    {
        uint32_t VertexCount = 0;
        uint32_t FrameCount = 0;
        float FramesPerSecond = 24.f;
        std::vector<Vector3> Positions;
        std::vector<std::array<uint32_t, 2>> Neighbors;

        struct SampleSpan
        {
            const Vector3* A;
            const Vector3* B;
            float Weight;
            glm::vec3 Position( uint32_t InVertex ) const
            {
                const auto& from = A[InVertex].InternalVector;
                return from + ( B[InVertex].InternalVector - from ) * Weight;
            }
        };

        SampleSpan Span( float InTime ) const
        {
            const float frame = std::clamp( InTime * FramesPerSecond, 0.f, static_cast<float>( FrameCount - 1 ) );
            const uint32_t a = static_cast<uint32_t>( frame );
            const uint32_t b = std::min( a + 1, FrameCount - 1 );
            return { Positions.data() + static_cast<size_t>( a ) * VertexCount,
                Positions.data() + static_cast<size_t>( b ) * VertexCount, frame - a };
        }

        static glm::mat3 Basis( const glm::vec3& a, const glm::vec3& b )
        {
            const glm::vec3 normal = glm::cross( a, b );
            if( glm::dot( a, a ) < 1e-16f || glm::dot( normal, normal ) < 1e-20f ) return glm::mat3( 1.f );
            const glm::vec3 x = glm::normalize( a );
            const glm::vec3 z = glm::normalize( normal );
            return glm::mat3( x, glm::cross( z, x ), z );
        }

        Vector3 Sample( uint32_t InVertex, float InTime ) const
        {
            return Vector3( Span( InTime ).Position( InVertex ) );
        }

        void Pose( const SampleSpan& InSpan, uint32_t InVertex,
            const PosNormTexTanBiVertex& InBind, PosNormTexTanBiVertex& OutVertex ) const
        {
            const auto& neighbors = Neighbors[InVertex];
            const glm::vec3 position = InSpan.Position( InVertex );
            const glm::mat3 basis = Basis( InSpan.Position( neighbors[0] ) - position,
                InSpan.Position( neighbors[1] ) - position );
            OutVertex.Position = Vector3( position );
            OutVertex.Normal = Vector3( basis * InBind.Normal.InternalVector );
            OutVertex.Tangent = Vector3( basis * InBind.Tangent.InternalVector );
            OutVertex.BiTangent = Vector3( basis * InBind.BiTangent.InternalVector );
            OutVertex.TextureCoord = InBind.TextureCoord;
        }
    };
}
