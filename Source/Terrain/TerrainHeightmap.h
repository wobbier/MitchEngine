#pragma once
#include "Terrain/TerrainSerialization.h"
#include <filesystem>

namespace TerrainData
{
    struct RawHeightmap
    {
        uint32_t Width = 0, Height = 0; // Includes the finite outer sample border.
        HeightEncoding Encoding = HeightEncoding::UInt16;
        bool BigEndian = false;
        bool FlipZ = false; // The first file row maps to the maximum terrain Z when true.
        double Offset = 0, Scale = 1;
        // UInt16 decodes to Offset + (value / 65535) * Scale; Float32 to Offset + value * Scale.
        // Export applies the inverse. Out-of-range values are rejected, never clamped.
    };
    bool ImportHeightmap( std::span<const uint8_t> InBytes, const Settings& InSettings, const RawHeightmap& InFormat,
        Heightfield& OutField, std::string& OutError );
    bool ExportHeightmap( const Heightfield& InField, const RawHeightmap& InFormat,
        std::vector<uint8_t>& OutBytes, std::string& OutError );
    bool ReadHeightmap( const std::filesystem::path& InPath, const Settings& InSettings, const RawHeightmap& InFormat,
        Heightfield& OutField, std::string& OutError );
    // Stages in the destination directory, then atomically replaces the destination file.
    bool WriteHeightmap( const std::filesystem::path& InPath, const Heightfield& InField, const RawHeightmap& InFormat, std::string& OutError );
}
