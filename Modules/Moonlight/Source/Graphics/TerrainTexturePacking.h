#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace Moonlight
{
    enum class TerrainTextureLayout { Array, Atlas };
    using TerrainTexturePaths = std::array<std::array<std::string, 3>, 4>; // Layer, base/normal/ORM.
    struct TerrainTextureUpload
    {
        uint16_t Layer = 0, Width = 0, Height = 0;
        uint8_t Mip = 0;
        std::vector<uint8_t> Pixels;
    };
    struct TerrainPackedTextures
    {
        TerrainTextureLayout Layout = TerrainTextureLayout::Array;
        uint16_t CellSize = 512;
        uint8_t AtlasMaxMip = 7; // Last independently padded level; never sample merged tail mips.
        std::array<std::vector<TerrainTextureUpload>, 3> Maps;
    };

    // CPU-only: bounded decode, resize, semantic mip filtering and independent atlas gutters.
    // Paths are already absolute/resolved by the main-thread adapter. Empty paths use neutral maps.
    // Cell size is a power of two in [8, 512]; production uses 512, tests may use smaller grids.
    bool PackTerrainTextures( const TerrainTexturePaths& InPaths, TerrainTextureLayout InLayout,
        TerrainPackedTextures& OutData, std::string& OutError, uint16_t InCellSize = 512 );
    // Source-independent stamps also include a newly created compiled twin, for hot reload.
    std::string TerrainTextureStamp( const TerrainTexturePaths& InPaths );
}
