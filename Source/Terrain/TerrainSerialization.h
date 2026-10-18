#pragma once

#include "Terrain/Heightfield.h"
#include <span>
#include <string_view>

namespace TerrainData
{
    enum class HeightEncoding : uint32_t { Float32 = 0, UInt16 = 1 };

    struct PageReference
    {
        TileCoord Coordinate;
        std::string Source; // Portable relative path; no parent traversal or absolute paths.
    };

    struct Manifest
    {
        uint64_t AssetGUID = 0;
        Settings Config;
        std::vector<PageReference> Pages; // Exactly one source page per declared tile.
    };

    struct TilePage
    {
        TileCoord Coordinate;
        std::vector<float> Heights;
    };

    struct CookInfo
    {
        uint64_t AssetGUID = 0;
        uint64_t SourceHash = 0;
        HeightEncoding Encoding = HeightEncoding::Float32;
        double QuantizationErrorBound = 0.0; // Includes Float32 decode rounding.
    };

    // Version 1 codecs operate on buffers only. Disk publication and asset registration belong
    // to the later resource/editor adapters. Every failure preserves the caller's output.
    bool SerializeManifest( const Manifest& InManifest, std::string& OutText, std::string& OutError );
    bool DeserializeManifest( std::string_view InText, Manifest& OutManifest, std::string& OutError );
    bool SerializePage( const Heightfield& InField, TileCoord InTile, std::vector<uint8_t>& OutData, std::string& OutError );
    bool DeserializePage( std::span<const uint8_t> InData, const Settings& InSettings, TilePage& OutPage, std::string& OutError );
    // Independently checksummed tiles with bounds and a random-access block directory. All
    // source tiles must be resident; UInt16 cooking rejects out-of-range heights (no clamping).
    bool CookHeightfield( const Heightfield& InField, uint64_t InAssetGUID, HeightEncoding InEncoding, std::vector<uint8_t>& OutData, std::string& OutError );
    bool ReadCookedHeightfield( std::span<const uint8_t> InData, Heightfield& OutField, CookInfo& OutInfo, std::string& OutError );
}
