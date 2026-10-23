#pragma once
#include "TreeRecipe.h"
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace MitchTree
{
    struct TreeVertex
    {
        Vector3 Position;
        Vector3 Normal;
        Vector2 UV;
        Vector3 Tangent;    // dP/du
        Vector3 Bitangent;  // dP/dv
        // R leaf flutter, G branch phase, B branch sway, A baked occlusion (all 0..255).
        std::array<uint8_t, 4> Wind{ { 0, 0, 0, 255 } };
    };

    // One material's geometry: shared vertices with an index list per level of detail (Lods[0] is
    // full detail). Every part of a tree has the same number of levels.
    struct TreePart
    {
        std::string Name;
        std::vector<TreeVertex> Vertices;
        std::vector<std::vector<uint32_t>> Lods;
    };

    struct TreeStatistics
    {
        uint32_t Stems = 0;
        uint32_t Leaves = 0;
        std::vector<uint32_t> Triangles;    // per level of detail, all parts
        double Milliseconds = 0.0;
    };

    struct TreeMesh
    {
        // "Bark" and (unless the recipe has no leaves) "Leaves".
        std::vector<TreePart> Parts;
        Vector3 BoundsMin;
        Vector3 BoundsMax;
        float Height = 0.f;
        // The branch sway weight a full wind-stream B channel stands for (sways scale with it, so
        // stiff trees move less than flexible ones).
        float MaxSway = 0.f;
        // Relative screen heights below which each simplified level draws (levels 1..n).
        std::vector<float> LodScreenHeights;
        TreeStatistics Statistics;

        const TreePart* FindPart( const std::string& InName ) const;
    };

    // Deterministic: the same recipe (including its seed) always produces the same mesh.
    TreeMesh Generate( const TreeRecipe& InRecipe );
}
