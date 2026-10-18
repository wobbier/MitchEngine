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
        std::vector<std::shared_ptr<const ResidentPatch>> Patches;
        std::vector<std::shared_ptr<const CollisionTile>> CollisionTiles;
        size_t BuiltPatches = 0;
        size_t BuiltCollisionTiles = 0;
        AABB Bounds;
    };

    // Worker-safe CPU preparation. The input must not mutate during the build. Failure preserves output.
    bool BuildResidentTerrain( const Heightfield& InField, uint64_t InAssetGUID, std::shared_ptr<const ResidentTerrain>& OutData, std::string& OutError );
    // Compares canonical samples with a compatible prior snapshot; unchanged geometry is shared.
    // Dirty rectangles are deliberately not required (saving can clear them between publications).
    bool UpdateResidentTerrain( const Heightfield& InField, uint64_t InAssetGUID, const std::shared_ptr<const ResidentTerrain>& InPrevious,
        std::shared_ptr<const ResidentTerrain>& OutData, std::string& OutError );
    // The first resident renderer/physics adapter supports world translation only, including parents.
    bool IsTranslationOnly( const Matrix4& InWorld );
}
