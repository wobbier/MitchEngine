#pragma once
#include "TreeRecipe.h"
#include <glm/vec3.hpp>
#include <cstdint>
#include <vector>

// Internal stages of MitchTree::Generate (exposed for tests and tools).
namespace MitchTree::Detail
{
    struct StemNode
    {
        glm::vec3 Position{ 0.f };
        glm::vec3 Direction{ 0.f, 1.f, 0.f };   // unit tangent
        // Rotation-minimizing frame (Normal, Binormal, Direction), right handed.
        glm::vec3 Normal{ 1.f, 0.f, 0.f };
        glm::vec3 Binormal{ 0.f, 0.f, 1.f };
        float Radius = 0.f;
        float Distance = 0.f;   // arclength from the stem's base
        float Sway = 0.f;       // accumulated wind sway weight (unnormalized)
        float Flare = 0.f;      // root buttress weight (trunk base)
    };

    struct Stem
    {
        uint64_t Id = 0;        // stable across edits of other levels (seeds this stem's randomness)
        int Level = 0;          // 0 trunk (and its leaders), 1.. branch levels
        int Parent = -1;        // index into Skeleton::Stems
        float ParentT = 0.f;    // attachment along the parent, 0..1 of its length
        std::vector<StemNode> Nodes;
        float Length = 0.f;
        float Nominal = 0.f;    // length at full crown ratio without variance (scales child counts)
        float Phase = 0.f;      // 0..1, shared by a first-level branch and everything on it
        float TrunkOffset = 0.f;// (level 0) path distance from the ground to this stem's base
        int ForkDepth = 0;
        bool CollapseTip = true;
    };

    struct Skeleton
    {
        std::vector<Stem> Stems;
        float Height = 0.f;     // nominal trunk height above the ground
        int DeepestLevel = 0;   // deepest level that produced stems
        float MaxSway = 0.f;
    };

    // Grows the trunk, its leaders and every enabled branch level, breadth first.
    Skeleton BuildSkeleton( const TreeRecipe& InRecipe );

    // Interpolated node at InT (0..1 of the stem's length).
    StemNode SampleStem( const Stem& InStem, float InT );

    // Stops runaway recipes: generation keeps going but adds no further stems past this.
    constexpr size_t MaxStems = 24000;
    constexpr size_t MaxNodesPerStem = 256;
}
