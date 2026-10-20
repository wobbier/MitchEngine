#pragma once
#include "Terrain/Heightfield.h"
#include "Math/Bounds.h"
#include <atomic>
#include <memory>

namespace TerrainData { struct StreamArchive; }
struct NavBuildInput;
struct NavBuildSettings;

// Immutable canonical source captured on the frame thread. Streamed sources pin only metadata;
// the bake worker reads selected height pages independently of visual/collision residency.
struct NavTerrainSource
{
    struct Region
    {
        TerrainData::TileCoord Tile;
        uint32_t X0 = 0, Z0 = 0, X1 = 0, Z1 = 0; // local quad range, exclusive maximum
    };
    std::shared_ptr<const TerrainData::Heightfield> Heights;
    std::shared_ptr<const TerrainData::StreamArchive> Archive;
    std::vector<Region> Regions;
    Vector3 Translation;
    uint8_t Area = 0;
    uint64_t Fingerprint = 0;
};

// No file reads. Computes a geometry-only fingerprint; paint and visual LODs are excluded.
// Bounds include Recast's border and rounded final tile. Failure preserves output.
bool CaptureNavTerrain( std::shared_ptr<const TerrainData::Heightfield> InHeights,
    std::shared_ptr<const TerrainData::StreamArchive> InArchive, const Vector3& InTranslation,
    uint8_t InArea, const AABB& InBounds, const NavBuildSettings& InSettings,
    NavTerrainSource& OutSource, std::string& OutError );

// Worker-only expansion. Uses canonical triangles; at most four million terrain triangles per bake.
// The caller owns scratch input and discards it on failure/cancellation.
bool AppendNavTerrainGeometry( NavBuildInput& InOutInput, std::string& OutError, std::atomic<bool>* InCancel = nullptr );
