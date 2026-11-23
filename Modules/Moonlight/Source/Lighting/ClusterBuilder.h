#pragma once
#include "RenderCommands.h"
#include "Math/Matrix4.h"
#include <algorithm>
#include <cstdint>
#include <glm/glm.hpp>
#include <vector>

namespace Moonlight
{
    // Assigns local lights to a 16x9x24 froxel grid (screen tiles x exponential depth slices)
    // for one camera. Each light's bounding sphere first gives a block of clusters (its slice range
    // and projected screen rectangle, the whole screen when it crosses the near plane), then the
    // sphere is tested against each cluster's view-space box in that block (ME-408), so a light
    // around the camera no longer lands in every tile of every slice it reaches.
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

        // Off: every cluster of a light's block takes it, without the per-cluster box test (the
        // pre-ME-408 lists, for comparisons: --cluster-block-bounds).
        static inline bool TestClusterBoxes = true;

        // InLights are the frame's local lights; cluster lists store indices into it.
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
        // The block of clusters each light touches this build, then per cluster its (capped) light
        // count and where its next index goes: lists are written straight into m_indices.
        struct LightCells
        {
            uint32_t Light;
            // The light's clusters: m_lightClusters[First, First + Count).
            uint32_t First, Count;
        };
        std::vector<LightCells> m_cells;
        std::vector<uint16_t> m_lightClusters;
        // Each cluster's view-space bounding box for this build's projection.
        std::vector<glm::vec3> m_clusterMin = std::vector<glm::vec3>( kClusterCount );
        std::vector<glm::vec3> m_clusterMax = std::vector<glm::vec3>( kClusterCount );
        bool m_haveClusterBoxes = false;
        void BuildClusterBoxes( const glm::mat4& InProjection, float InNear, float InFar );
        std::vector<uint32_t> m_counts = std::vector<uint32_t>( kClusterCount, 0 );
        std::vector<uint32_t> m_cursors = std::vector<uint32_t>( kClusterCount, 0 );
        Params m_params;
        uint32_t m_usedIndices = 0;
    };
}
