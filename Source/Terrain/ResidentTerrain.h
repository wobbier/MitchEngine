#pragma once

#include "Terrain/TerrainMesh.h"
#include <memory>

namespace TerrainData
{
    struct ResidentPatch
    {
        PatchMesh Mesh;
        Vector3 Offset;
    };

    struct CollisionTile
    {
        TileCoord Coordinate;
        // Terrain-local positions, shared by instances; collision stays at canonical resolution.
        std::vector<Vector3> Positions;
        std::vector<uint32_t> Indices;
    };

    struct ResidentTerrain
    {
        uint64_t AssetGUID = 0;
        std::shared_ptr<const Heightfield> Heights;
        std::vector<ResidentPatch> Patches;
        std::vector<CollisionTile> CollisionTiles;
        AABB Bounds;
    };

    // Worker-safe CPU preparation. The input must not mutate during the build. Failure preserves output.
    bool BuildResidentTerrain( const Heightfield& InField, uint64_t InAssetGUID, std::shared_ptr<const ResidentTerrain>& OutData, std::string& OutError );
    // The first resident renderer/physics adapter supports world translation only, including parents.
    bool IsTranslationOnly( const Matrix4& InWorld );
}
