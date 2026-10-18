#pragma once
#include "Terrain/TerrainSerialization.h"
#include <filesystem>

namespace TerrainData
{
    // A self-contained unsaved snapshot. The base manifest is the save conflict precondition;
    // recovery never publishes back to the source asset automatically.
    struct RecoveryInfo
    {
        std::string AssetPath;
        uint64_t AssetGUID = 0;
        uint64_t SavedAt = 0; // Unix seconds, UTC.
        std::string BaseManifest;
    };

    bool SerializeRecovery( const RecoveryInfo& InInfo, const Heightfield& InField, std::vector<uint8_t>& OutBytes, std::string& OutError );
    bool DeserializeRecovery( std::span<const uint8_t> InBytes, RecoveryInfo& OutInfo, Heightfield& OutField, std::string& OutError );
    // Metadata inspection reads only the bounded header/metadata. Payload validation happens
    // on restore; corrupt snapshots remain visible to the recovery manager.
    bool InspectRecovery( const std::filesystem::path& InPath, RecoveryInfo& OutInfo, std::string& OutError );
    bool ReadRecovery( const std::filesystem::path& InPath, RecoveryInfo& OutInfo, Heightfield& OutField, std::string& OutError );
    // Single-file, same-directory atomic publication. Failures preserve the previous snapshot.
    bool WriteRecovery( const std::filesystem::path& InPath, const RecoveryInfo& InInfo, const Heightfield& InField, std::string& OutError );
}
