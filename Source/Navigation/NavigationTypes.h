#pragma once
#include "Math/Vector3.h"
#include <cstdint>

// Navigation areas: every walkable surface belongs to one of NavAreas::kCount areas. An area has a
// name and a traversal cost (Project Settings > Navigation); agents and queries pick the areas they
// may use with an area mask (bit i = area i).
namespace NavAreas
{
    constexpr int kCount = 16;
    constexpr uint8_t Walkable = 0;
    constexpr uint8_t NotWalkable = 1;   // removed from the navmesh
    constexpr uint8_t Jump = 2;          // the default for NavMeshLinks
    constexpr uint32_t All = 0xFFFFu;

    constexpr uint32_t Bit( uint8_t InArea )
    {
        return 1u << ( InArea % kCount );
    }
}

// How Recast splits the walkable surface into regions before building polygons.
enum class NavPartition : uint8_t
{
    Watershed = 0,  // best polygons, slowest; the usual choice for pre-baked levels
    Monotone,       // fastest, long thin polygons; good for runtime rebuilds
    Layers,         // between the two; handles overlapping floors with small tiles well
};

// Bake settings (NavMeshSurface). Lengths are in world units; "cells" are voxels of CellSize.
struct NavBuildSettings
{
    float AgentRadius = 0.5f;
    float AgentHeight = 2.f;
    float AgentMaxClimb = 0.5f;     // step height
    float AgentMaxSlope = 45.f;     // degrees
    float CellSize = 0.f;           // 0 = AgentRadius / 3
    float CellHeight = 0.f;         // 0 = half the cell size
    int TileSize = 64;              // cells per tile side
    float MinRegionSize = 8.f;      // cells (side): smaller islands are dropped
    float MergeRegionSize = 20.f;   // cells (side): smaller regions merge into neighbours
    float EdgeMaxLength = 12.f;
    float EdgeMaxError = 1.3f;      // cells
    float DetailSampleDistance = 6.f;   // cells; height detail sampling
    float DetailSampleMaxError = 1.f;   // cell heights
    NavPartition Partition = NavPartition::Watershed;
    bool FilterLowHangingObstacles = true;
    bool FilterLedgeSpans = true;
    bool FilterLowHeightSpans = true;

    float GetCellSize() const { return CellSize > 0.f ? CellSize : AgentRadius / 3.f; }
    float GetCellHeight() const { return CellHeight > 0.f ? CellHeight : GetCellSize() * 0.5f; }
    uint64_t Hash() const;
};

enum class NavPathStatus : uint8_t
{
    Invalid = 0,    // start or end isn't on (or near) the navmesh
    Partial,        // the end can't be reached; the path leads to the closest reachable point
    Complete,
};

// A point on the navmesh (SamplePosition, Raycast).
struct NavMeshHit
{
    Vector3 Position;
    Vector3 Normal;         // Raycast: the wall that was hit
    float Distance = 0.f;
    uint8_t Area = 0;
    bool Hit = false;
};

// Which areas a query may cross. Costs come from Project Settings.
struct NavQueryFilter
{
    uint32_t AreaMask = NavAreas::All;
    // Query the navmesh baked for this agent type (1-based); 0 = the one under the query point.
    int AgentType = 0;
};
