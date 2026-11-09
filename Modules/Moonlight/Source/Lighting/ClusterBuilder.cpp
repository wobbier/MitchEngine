#include "ClusterBuilder.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace Moonlight
{
    namespace
    {
        // The sphere around everything a light can reach. A spot lights only its cone out to Range
        // (a spherical sector: Lighting.sh's localLightCone is zero past the outer angle), so it
        // gets the smallest sphere around that sector instead of the whole Range sphere: through
        // the apex and the rim up to 45 degrees, centred on the rim's circle up to 90.
        void LightBounds( const LightCommand& InLight, glm::vec3& OutCenter, float& OutRadius )
        {
            OutCenter = InLight.Position.InternalVector;
            OutRadius = InLight.GetBoundingRadius();
            const float axisLength = InLight.Direction.Length();
            const float cosOuter = std::clamp( InLight.CosOuter, -1.f, 1.f );
            if( InLight.Type != LightType::Spot || axisLength < 1e-6f || cosOuter <= 0.f )
            {
                return;
            }
            const glm::vec3 axis = InLight.Direction.InternalVector / axisLength;
            float along = 0.f;
            if( cosOuter >= 0.70710678f )
            {
                along = OutRadius = InLight.Range / ( 2.f * cosOuter );
            }
            else
            {
                along = InLight.Range * cosOuter;
                OutRadius = InLight.Range * std::sqrt( 1.f - cosOuter * cosOuter );
            }
            OutCenter += axis * along;
            OutRadius *= 1.001f;   // rounding slack: the bound stays outside the cone
        }
    }


    void ClusterBuilder::Build( const std::vector<LightCommand>& InLights, const Matrix4& InView, const Matrix4& InProjection, float InNear, float InFar, float InWidth, float InHeight )
    {
        const float nearPlane = std::max( InNear, 0.001f );
        const float farPlane = std::max( InFar, nearPlane + 0.01f );
        const float logRatio = std::log( farPlane / nearPlane );
        m_params.TileWidth = std::max( InWidth, 1.f ) / kGridX;
        m_params.TileHeight = std::max( InHeight, 1.f ) / kGridY;
        m_params.SliceScale = kGridZ / logRatio;
        m_params.SliceBias = -static_cast<float>( kGridZ ) * std::log( nearPlane ) / logRatio;

        m_cells.clear();

        const glm::mat4& view = InView.GetInternalMatrix();
        const glm::mat4& projection = InProjection.GetInternalMatrix();
        auto sliceOf = [&]( float z ) {
            return static_cast<int>( std::floor( std::log( std::max( z, 1e-4f ) ) * m_params.SliceScale + m_params.SliceBias ) );
        };

        for( uint32_t lightIndex = 0; lightIndex < InLights.size(); ++lightIndex )
        {
            const LightCommand& light = InLights[lightIndex];
            glm::vec3 bound;
            float radius = 0.f;
            LightBounds( light, bound, radius );
            const glm::vec4 center = view * glm::vec4( bound, 1.f );
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

            m_cells.push_back( { lightIndex, tileMinX, tileMaxX, tileMinY, tileMaxY, sliceMin, sliceMax } );
        }

        // Count each cluster's lights (the first kMaxLightsPerCluster by index), lay the lists out
        // back to back, then write each light's index into every cluster it touches, in light
        // order: no per-cluster lists to grow and copy.
        std::fill( m_counts.begin(), m_counts.end(), 0u );
        for( const LightCells& cells : m_cells )
        {
            for( int z = cells.MinZ; z <= cells.MaxZ; ++z )
            {
                for( int y = cells.MinY; y <= cells.MaxY; ++y )
                {
                    uint32_t* row = m_counts.data() + y * kGridX + z * kGridX * kGridY;
                    for( int x = cells.MinX; x <= cells.MaxX; ++x )
                    {
                        row[x] += row[x] < kMaxLightsPerCluster ? 1u : 0u;
                    }
                }
            }
        }
        uint32_t offset = 0;
        float* const grid = m_grid.data();
        for( uint32_t cluster = 0; cluster < kClusterCount; ++cluster )
        {
            const uint32_t count = std::min( m_counts[cluster], kMaxIndices - offset );
            grid[cluster * 4 + 0] = static_cast<float>( offset );
            grid[cluster * 4 + 1] = static_cast<float>( count );
            m_counts[cluster] = count;   // now the room left in its list
            m_cursors[cluster] = offset;
            offset += count;
        }
        uint32_t* const room = m_counts.data();
        uint32_t* const cursors = m_cursors.data();
        float* const indices = m_indices.data();
        for( const LightCells& cells : m_cells )
        {
            const float light = static_cast<float>( cells.Light );
            for( int z = cells.MinZ; z <= cells.MaxZ; ++z )
            {
                for( int y = cells.MinY; y <= cells.MaxY; ++y )
                {
                    const uint32_t first = y * kGridX + z * kGridX * kGridY;
                    for( uint32_t cluster = first + cells.MinX; cluster <= first + cells.MaxX; ++cluster )
                    {
                        if( room[cluster] > 0 )
                        {
                            --room[cluster];
                            indices[cursors[cluster]++] = light;
                        }
                    }
                }
            }
        }
        m_usedIndices = offset;
    }
}
