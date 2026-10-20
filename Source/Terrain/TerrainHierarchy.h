#pragma once
#include "Terrain/ResidentTerrain.h"
#include <functional>
#include <span>

namespace TerrainData
{
    struct StreamArchive;
    struct StreamCoverageTile;
    struct CoverageNode
    {
        TileCoord Coordinate;
        uint8_t Level = 0;
        int32_t Parent = -1, Root = -1;
        std::array<int32_t, 4> Children{ -1, -1, -1, -1 };
        uint64_t Offset = 0, Size = 0, Checksum = 0, DecodedSize = 0;
        float MinHeight = 0, MaxHeight = 0;
    };
    struct CoverageView
    {
        Vector3 Position;
        float FineRadius = 0, CoverageRadius = 1024;
    };
    // Deterministic dyadic forest, anchored at MinTile; partial rectangles are never padded.
    bool BuildCoverageHierarchy( const Settings& InSettings, std::vector<CoverageNode>& OutNodes, std::vector<uint32_t>& OutRoots, std::string& OutError );
    bool BuildHierarchyPage( const Heightfield& InField, const CoverageNode& InNode, uint8_t InBase,
        std::shared_ptr<const StreamCoverageTile>& OutPage, std::string& OutError );
    // Returns a balanced complete cut plus its ancestor requests, bounded independently of extent.
    bool SelectCoverageRequests( const StreamArchive& InArchive, std::span<const CoverageView> InViews, size_t InMaxCut,
        std::vector<uint32_t>& OutNodes, std::string& OutError );
    // Publishes complete sibling groups and coarsens unavailable transition bands to ready ancestors.
    std::vector<uint32_t> ReadyCoverageCut( const StreamArchive& InArchive, const std::function<bool( uint32_t )>& InWanted,
        const std::function<bool( uint32_t )>& InReady );
    void BuildHierarchyNeighbors( ResidentTerrain& InTerrain );
}
