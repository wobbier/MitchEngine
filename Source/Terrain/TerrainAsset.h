#pragma once
#include "Terrain/TerrainSerialization.h"
#include <filesystem>

namespace TerrainData
{
    // Source-only file adapter. On failure, outputs and the previously published asset survive.
    // Optional sample admission runs after manifest validation and before reading page payloads.
    bool LoadSource( const std::filesystem::path& InPath, Heightfield& OutField, uint64_t& OutGUID,
        std::string& OutManifestText, std::string& OutError, uint64_t InMaxSamples = UINT64_MAX );
    // Every save writes a unique companion generation and atomically replaces only the manifest.
    // ExpectedText detects conflicting external saves. Null is for creating a new asset only.
    // Old generations are retained; cleanup/compaction is a separate future operation.
    bool SaveSource( const std::filesystem::path& InPath, const Heightfield& InField, uint64_t InGUID,
        const std::string* InExpectedText, std::string& OutManifestText, std::string& OutError );
}
