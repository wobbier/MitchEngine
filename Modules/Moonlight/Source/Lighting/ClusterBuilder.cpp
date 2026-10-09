#include "ClusterBuilder.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace Moonlight
{
    void ClusterBuilder::Build( const std::vector<LightCommand>& InLights, const Matrix4& InView, const Matrix4& InProjection, float InNear, float InFar, float InWidth, float InHeight )
    {
        const float nearPlane = std::max( InNear, 0.001f );
        const float farPlane = std::max( InFar, nearPlane + 0.01f );
        const float logRatio = std::log( farPlane / nearPlane );
        m_params.TileWidth = std::max( InWidth, 1.f ) / kGridX;
        m_params.TileHeight = std::max( InHeight, 1.f ) / kGridY;
        m_params.SliceScale = kGridZ / logRatio;
        m_params.SliceBias = -static_cast<float>( kGridZ ) * std::log( nearPlane ) / logRatio;

        for( std::vector<uint32_t>& list : m_lists )
        {
            list.clear();
        }

        const glm::mat4& view = InView.GetInternalMatrix();
        const glm::mat4& projection = InProjection.GetInternalMatrix();
        auto sliceOf = [&]( float z ) {
            return static_cast<int>( std::floor( std::log( std::max( z, 1e-4f ) ) * m_params.SliceScale + m_params.SliceBias ) );
        };

        for( uint32_t lightIndex = 0; lightIndex < InLights.size(); ++lightIndex )
        {
            const LightCommand& light = InLights[lightIndex];
            const glm::vec4 center = view * glm::vec4( light.Position.InternalVector, 1.f );
            const float radius = light.Range;
            if( center.z + radius < nearPlane || center.z - radius > farPlane )
            {
                continue;
            }
            const int sliceMin = std::clamp( sliceOf( std::max( center.z - radius, nearPlane ) ), 0, static_cast<int>( kGridZ ) - 1 );
            const int sliceMax = std::clamp( sliceOf( std::min( center.z + radius, farPlane ) ), 0, static_cast<int>( kGridZ ) - 1 );

            // Screen rectangle of the sphere's view-space box (full screen if it crosses the near plane).
            int tileMinX = 0, tileMaxX = kGridX - 1, tileMinY = 0, tileMaxY = kGridY - 1;
            if( center.z - radius > nearPlane )
            {
                float minX = std::numeric_limits<float>::max(), maxX = -std::numeric_limits<float>::max();
                float minY = std::numeric_limits<float>::max(), maxY = -std::numeric_limits<float>::max();
                for( int corner = 0; corner < 8; ++corner )
                {
                    const glm::vec4 point( center.x + ( ( corner & 1 ) ? radius : -radius ), center.y + ( ( corner & 2 ) ? radius : -radius ), center.z + ( ( corner & 4 ) ? radius : -radius ), 1.f );
                    const glm::vec4 clip = projection * point;
                    const float w = std::max( clip.w, 1e-4f );
                    minX = std::min( minX, clip.x / w );
                    maxX = std::max( maxX, clip.x / w );
                    minY = std::min( minY, clip.y / w );
                    maxY = std::max( maxY, clip.y / w );
                }
                if( maxX < -1.f || minX > 1.f || maxY < -1.f || minY > 1.f )
                {
                    continue;
                }
                // NDC y up -> tile rows from the top.
                tileMinX = std::clamp( static_cast<int>( ( minX * 0.5f + 0.5f ) * kGridX ), 0, static_cast<int>( kGridX ) - 1 );
                tileMaxX = std::clamp( static_cast<int>( ( maxX * 0.5f + 0.5f ) * kGridX ), 0, static_cast<int>( kGridX ) - 1 );
                tileMinY = std::clamp( static_cast<int>( ( 0.5f - maxY * 0.5f ) * kGridY ), 0, static_cast<int>( kGridY ) - 1 );
                tileMaxY = std::clamp( static_cast<int>( ( 0.5f - minY * 0.5f ) * kGridY ), 0, static_cast<int>( kGridY ) - 1 );
            }

            for( int z = sliceMin; z <= sliceMax; ++z )
            {
                for( int y = tileMinY; y <= tileMaxY; ++y )
                {
                    for( int x = tileMinX; x <= tileMaxX; ++x )
                    {
                        std::vector<uint32_t>& list = m_lists[x + y * kGridX + z * kGridX * kGridY];
                        if( list.size() < kMaxLightsPerCluster )
                        {
                            list.push_back( lightIndex );
                        }
                    }
                }
            }
        }

        // Flatten into the grid (offset, count) and the index list.
        uint32_t offset = 0;
        for( uint32_t cluster = 0; cluster < kClusterCount; ++cluster )
        {
            std::vector<uint32_t>& list = m_lists[cluster];
            const uint32_t count = std::min<uint32_t>( static_cast<uint32_t>( list.size() ), kMaxIndices - offset );
            m_grid[cluster * 4 + 0] = static_cast<float>( offset );
            m_grid[cluster * 4 + 1] = static_cast<float>( count );
            for( uint32_t i = 0; i < count; ++i )
            {
                m_indices[offset + i] = static_cast<float>( list[i] );
            }
            offset += count;
        }
        m_usedIndices = offset;
    }
}
