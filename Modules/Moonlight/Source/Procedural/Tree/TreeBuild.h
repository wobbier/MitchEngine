#pragma once
#include "TreeGenerator.h"
#include "TreeSkeleton.h"
#include <glm/vec3.hpp>
#include <vector>

// Internal geometry stages of MitchTree::Generate.
namespace MitchTree::Detail
{
    // Occlusion probes and light blockers gathered while building geometry.
    struct OcclusionSample
    {
        glm::vec3 Position{ 0.f };
        float Area = 0.f;               // light-blocking area (m^2) added to the density grid
        uint32_t FirstVertex = 0;       // vertices that receive this sample's occlusion
        uint32_t VertexCount = 0;
        bool Leaf = false;              // the vertices are leaf vertices (else bark)
    };

    struct BuildContext
    {
        const TreeRecipe& Recipe;
        const Skeleton& Tree;
        int LodLevels = 0;
        std::vector<OcclusionSample> Samples;
    };

    // Tubes along every stem, with index-only levels of detail (coarser rings and sides, thin stems
    // dropped). Fills OutPart.Lods with 1 + LodLevels lists.
    void BuildBark( BuildContext& InContext, TreePart& OutPart );
    // A full-detail leaf card, remembered for the coarser levels (built after occlusion is baked).
    struct LeafCard
    {
        uint32_t Corners[4] = {};       // full-detail vertices: base left, base right, tip left, tip right
        glm::vec3 Center{ 0.f };
        glm::vec3 Normal{ 0.f, 1.f, 0.f };
        float Keep = 0.f;               // 0..1: kept at level l while below LeafKeep^l
    };

    // Leaf cards (or frond ribbons, whose levels are filled here) on the outermost stems.
    void BuildLeaves( BuildContext& InContext, TreePart& OutPart, std::vector<LeafCard>& OutCards );
    // Coarser leaf levels: nested subsets of the cards, enlarged to cover the same crown.
    void BuildLeafLods( BuildContext& InContext, TreePart& InOutPart, const std::vector<LeafCard>& InCards );
    // Bakes crown self-occlusion into the alpha wind channel of the probed vertices.
    void BakeOcclusion( BuildContext& InContext, std::vector<TreeVertex>& InOutBark, std::vector<TreeVertex>* InOutLeaves );

    // Appends a triangle wound to face InFacing (the engine's front face is cross(b - a, c - a));
    // degenerate triangles are dropped.
    void AddTriangle( const std::vector<TreeVertex>& InVertices, std::vector<uint32_t>& OutIndices, uint32_t InA, uint32_t InB, uint32_t InC, const glm::vec3& InFacing );
    uint8_t ToByte( float InUnit );
}
