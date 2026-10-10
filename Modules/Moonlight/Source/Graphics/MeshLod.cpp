#include "MeshLod.h"
#include "meshoptimizer.h"
#include <algorithm>
#include <cmath>

namespace Moonlight
{
    std::vector<MeshLod> BuildMeshLods( const float* InPositions, size_t InVertexCount, size_t InPositionStride, const std::vector<uint32_t>& InIndices, const MeshLodSettings& InSettings )
    {
        std::vector<MeshLod> lods;
        if( InSettings.Levels <= 0 || !InPositions || InVertexCount == 0 || InIndices.size() < 3 || InIndices.size() / 3 < InSettings.MinTriangles )
        {
            return lods;
        }
        const float reduction = std::clamp( InSettings.Reduction, 0.05f, 0.95f );
        const std::vector<uint32_t>* previous = &InIndices;
        float screenHeight = InSettings.TransitionHeight;
        // Each level shows at half the size of the one before, so it may deviate twice as much for
        // the same error on screen.
        float error = InSettings.MaxError;
        for( int level = 0; level < InSettings.Levels; ++level )
        {
            const size_t target = std::max<size_t>( 3, static_cast<size_t>( static_cast<float>( previous->size() / 3 ) * reduction ) * 3 );
            std::vector<uint32_t> simplified( previous->size() );
            float resultError = 0.f;
            // Simplifying the previous level (not the full mesh) keeps levels nested and is cheaper.
            // Small disconnected pieces may vanish once they're below the error.
            size_t count = meshopt_simplify( simplified.data(), previous->data(), previous->size(), InPositions, InVertexCount, InPositionStride, target, error, meshopt_SimplifyPrune, &resultError );
            if( count > previous->size() * 9 / 10 )
            {
                // Stuck on attribute seams: flat-shaded or unwelded meshes (no shared vertices) are
                // all seams. Collapse across them; at this size the borrowed normals don't show.
                count = meshopt_simplify( simplified.data(), previous->data(), previous->size(), InPositions, InVertexCount, InPositionStride, target, error, meshopt_SimplifyPrune | meshopt_SimplifyPermissive, &resultError );
            }
            // Not meaningfully smaller: the error budget or the topology won't allow more.
            if( count < 3 || count > previous->size() * 9 / 10 )
            {
                break;
            }
            simplified.resize( count );
            meshopt_optimizeVertexCache( simplified.data(), simplified.data(), count, InVertexCount );

            MeshLod lod;
            lod.Indices = std::move( simplified );
            lod.ScreenHeight = screenHeight;
            lods.push_back( std::move( lod ) );
            previous = &lods.back().Indices;
            screenHeight *= 0.5f;
            error *= 2.f;
        }
        return lods;
    }


    float RelativeScreenHeight( float InRadius, float InDistance, float InVerticalFovDegrees, bool InOrthographic, float InOrthographicSize )
    {
        if( InOrthographic )
        {
            // OrthographicSize is half the view's height.
            return InOrthographicSize > 0.f ? InRadius / InOrthographicSize : 1.f;
        }
        const float halfFov = std::tan( InVerticalFovDegrees * 0.5f * 3.14159265f / 180.f );
        const float distance = std::max( InDistance, 1e-4f );
        return halfFov > 0.f ? InRadius / ( distance * halfFov ) : 1.f;
    }


    uint8_t SelectLod( float InScreenHeight, const float* InLodHeights, size_t InLodCount, float InBias )
    {
        const float height = InScreenHeight * std::max( InBias, 0.f );
        uint8_t level = 0;
        for( size_t i = 0; i < InLodCount; ++i )
        {
            if( height < InLodHeights[i] )
            {
                level = static_cast<uint8_t>( i + 1 );
            }
        }
        return level;
    }
}
