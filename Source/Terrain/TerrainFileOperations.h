#pragma once
#include "Terrain/TerrainAsset.h"

namespace TerrainData
{
    struct SourceTransfer
    {
        uint64_t AssetGUID = 0;
        std::string BeforeManifest, AfterManifest;
    };

    // Publishes an independent current generation and metadata at a new location. Duplicate gives
    // it a fresh identity; move preserves identity and retires the original manifest/metadata last.
    // Previous source generations remain available for other manifests and history/compaction.
    // No concurrent-writer lock or multi-file crash transaction is implied.
    bool TransferSource( const std::filesystem::path& InFrom, const std::filesystem::path& InTo, bool InDuplicate,
        SourceTransfer& OutTransfer, std::string& OutError );
}
