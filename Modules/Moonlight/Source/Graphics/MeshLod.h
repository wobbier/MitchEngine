#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace Moonlight
{
    // How a model's meshes get levels of detail (its import settings; see ModelResourceMetadata).
    struct MeshLodSettings
    {
        // Simplified levels after the full mesh (0 = none).
        int Levels = 2;
        // Each level keeps this fraction of the previous level's triangles.
        float Reduction = 0.5f;
        // Relative screen height (the mesh's bounding sphere over the view's height) below which
        // level 1 draws; each further level switches at half the previous height.
        float TransitionHeight = 0.3f;
        // Meshes with fewer triangles keep full detail only.
        uint32_t MinTriangles = 256;
        // Largest deviation a level may introduce, relative to the mesh's size.
        float MaxError = 0.02f;
    };

    // One level of detail: an index list over the full mesh's vertices.
    struct MeshLod
    {
        std::vector<uint32_t> Indices;
        // Drawn while the mesh covers less than this relative screen height.
        float ScreenHeight = 0.f;
    };

    // Simplifies a triangle list (meshoptimizer; attribute seams found from coincident positions are
    // kept closed) into progressively coarser levels sharing the same vertices. Stops early when a
    // level can't get meaningfully smaller than the one before it.
    std::vector<MeshLod> BuildMeshLods( const float* InPositions, size_t InVertexCount, size_t InPositionStride, const std::vector<uint32_t>& InIndices, const MeshLodSettings& InSettings );

    // Relative screen height of a bounding sphere: its diameter over the height the view sees at its
    // distance (perspective), or over the view's height (orthographic). Can exceed 1 when close.
    float RelativeScreenHeight( float InRadius, float InDistance, float InVerticalFovDegrees, bool InOrthographic, float InOrthographicSize );

    // The level to draw (0 = full detail) for a mesh covering InScreenHeight; InBias scales the
    // height first (above 1 keeps detail longer).
    uint8_t SelectLod( float InScreenHeight, const float* InLodHeights, size_t InLodCount, float InBias = 1.f );
}
