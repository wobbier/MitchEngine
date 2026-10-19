#pragma once
#include "Graphics/TerrainTexturePacking.h"
#include <filesystem>

namespace Moonlight
{
    struct TerrainTextureCacheOptions
    {
        std::filesystem::path Directory = ".tmp/Cache/TerrainTextures";
        uint64_t MaxBytes = 512ull << 20;
        bool Enabled = true;
    };

    struct TerrainTextureCacheInfo
    {
        bool Hit = false, Stored = false;
        std::filesystem::path File;
        // Cache failures are nonfatal; source/packing failures still use OutError.
        std::string Warning;
    };

    // Worker-only content fingerprinting, checked cache reads and atomic publication.
    // Failure preserves OutData. Unavailable/corrupt caches fall back to the ordinary packer.
    bool LoadOrPackTerrainTextures( const TerrainTexturePaths& InPaths, TerrainTextureLayout InLayout,
        const TerrainTextureCacheOptions& InOptions, TerrainPackedTextures& OutData, std::string& OutError,
        TerrainTextureCacheInfo& OutInfo, uint16_t InCellSize = 512 );
}
