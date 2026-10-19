#pragma once
#include "Terrain/ResidentTerrain.h"
#include <filesystem>
#include <span>

namespace TerrainData
{
    struct StreamBlock
    {
        TileCoord Coordinate;
        uint64_t Offset = 0, Size = 0, Checksum = 0;
        float MinHeight = 0, MaxHeight = 0;
    };

    struct StreamTile
    {
        TileCoord Coordinate;
        std::vector<float> Heights;
        std::vector<std::shared_ptr<const ResidentPatch>> Patches;
        std::shared_ptr<const CollisionTile> Collision;
    };

    struct StreamArchive
    {
        std::filesystem::path Path;
        Settings Config;
        uint64_t AssetGUID = 0, Revision = 0, FileSize = 0;
        uint8_t CoverageLod = 0;
        std::vector<uint8_t> Header;
        std::vector<StreamBlock> Blocks;
        std::shared_ptr<const ResidentTerrain> Coverage;
        size_t TileIndex( TileCoord InTile ) const;
    };

    // Version 2 .terrainc: bounded resident coverage followed by independent Float32 tile blocks.
    // Cooking streams to a neighboring temporary file and atomically publishes only on success.
    bool CookStreamArchive( const std::filesystem::path& InPath, const Heightfield& InField, uint64_t InGUID,
        uint8_t InCoverageLod, std::string& OutError );
    // Reads the header, directory and coarse coverage only. No canonical tile payload is read.
    bool OpenStreamArchive( const std::filesystem::path& InPath, std::shared_ptr<const StreamArchive>& OutArchive, std::string& OutError );
    // Worker-only file I/O and CPU preparation. Rechecks the immutable archive identity.
    bool ReadStreamTile( const StreamArchive& InArchive, TileCoord InTile, std::shared_ptr<const StreamTile>& OutTile, std::string& OutError );
    bool IsStreamArchive( std::span<const uint8_t> InPrefix );
    void BuildPatchNeighbors( ResidentTerrain& InTerrain );
}
