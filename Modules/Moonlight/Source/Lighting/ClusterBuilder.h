#pragma once
#include "RenderCommands.h"
#include "Math/Matrix4.h"
#include <algorithm>
#include <cstdint>
#include <vector>

namespace Moonlight
{
    // Assigns point/spot lights to a 16x9x24 froxel grid (screen tiles x exponential depth slices)
    // for one camera. Lights are tested as bounding spheres against each slice and their projected
    // screen rectangle, which is conservative but cheap.
    class ClusterBuilder
    {
    public:
        static constexpr uint32_t kGridX = 16;
        static constexpr uint32_t kGridY = 9;
        static constexpr uint32_t kGridZ = 24;
        static constexpr uint32_t kClusterCount = kGridX * kGridY * kGridZ;
        static constexpr uint32_t kMaxLightsPerCluster = 64;
        static constexpr uint32_t kIndexWidth = 1024;
        static constexpr uint32_t kIndexHeight = 128;
        static constexpr uint32_t kMaxIndices = kIndexWidth * kIndexHeight;

        struct Params
        {
            float TileWidth = 1.f;
            float TileHeight = 1.f;
            float SliceScale = 1.f;
            float SliceBias = 0.f;
        };

        // InLights are the frame's point/spot lights; cluster lists store indices into it.
        void Build( const std::vector<LightCommand>& InLights, const Matrix4& InView, const Matrix4& InProjection, float InNear, float InFar, float InWidth, float InHeight );

        // RGBA32F: x offset into the index list, y count (per cluster, x + y * kGridX columns, z rows).
        const std::vector<float>& GetGrid() const { return m_grid; }
        // R32F: light indices.
        const std::vector<float>& GetIndices() const { return m_indices; }
        const Params& GetParams() const { return m_params; }
        // Rows of the index texture that hold data this frame.
        uint32_t GetUsedIndexRows() const { return std::max<uint32_t>( 1, ( m_usedIndices + kIndexWidth - 1 ) / kIndexWidth ); }

    private:
        std::vector<float> m_grid = std::vector<float>( kClusterCount * 4, 0.f );
        std::vector<float> m_indices = std::vector<float>( kMaxIndices, 0.f );
        std::vector<std::vector<uint32_t>> m_lists = std::vector<std::vector<uint32_t>>( kClusterCount );
        Params m_params;
        uint32_t m_usedIndices = 0;
    };
}
