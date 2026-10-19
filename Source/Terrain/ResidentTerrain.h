#pragma once

#include "Terrain/TerrainMesh.h"
#include <memory>
#include <array>

namespace TerrainData
{
    struct ResidentPatch
    {
        PatchMesh Mesh;
        Vector3 Offset;
        // Global sample stride of the stored vertices (0 for canonical resident patches).
        uint8_t BaseLod = 0;
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
        // MinX, MaxX, MinZ, MaxZ; -1 is the finite outside boundary.
        std::vector<std::array<int32_t, 4>> PatchNeighbors;
        size_t BuiltPatches = 0;
        size_t BuiltCollisionTiles = 0;
        AABB Bounds;
        bool Streamed = false;
        uint64_t StreamRevision = 0;
        // Retain cache reservations while a draw, query snapshot or collider still owns data.
        std::vector<std::shared_ptr<const void>> ResidencyPins;
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
