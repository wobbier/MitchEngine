#pragma once
#include <atomic>
#include <cstdint>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "JSON.h"

// A cooked tree's index: AssetRegistry.json at the tree's root, written by the cooker (SHIP-2,
// Engine/Docs/Cooking.md). A cooked tree ships cooked twins (.dds, .assbin, shader binaries)
// instead of sources, and no .meta files; the registry answers what those used to:
//
//   { "Version": 1, "Platform": "linux",
//     "Assets": { "Assets/Textures/X.png": { "Stripped": true, "GUID": "<16 hex>", "Meta": { ... },
//                                             "Textures": { "<model ref>": "Assets/..." } } },
//     "EngineShaderStages": [ "Assets/Shaders/Standard.vert", ... ] }
//
// - "Stripped": the source isn't shipped. A stat-only mount below every other one answers that it
//   exists, so Path( "X.png" ).Exists holds and loaders go on to read the cooked twin.
// - "Meta": the asset's .meta JSON, for the import settings loaders still read (model LODs, ...).
// - "GUID": what AssetDatabase scans .meta files and prefabs for in source trees.
// - "Textures" (models): where each material texture reference resolved at cook time, so cooked
//   runs never search the filesystem for a near match.
// Keys are logical paths in the overlay spelling ("Assets/..."), as ResourceCache::CacheKey makes.
class CookedAssets
{
public:
    static constexpr const char* kRegistryPath = "AssetRegistry.json";
    // A Build Game output's content: this pak beside the executable. The game mounts it on its
    // own when no --pak names another (Engine/Docs/Building.md).
    static constexpr const char* kShippedPak = "Game.mpak";

    static CookedAssets& Get();

    // Engine::Init: reads the registry from the overlay roots when the tree has one and mounts its
    // stripped sources. Returns IsCooked().
    bool Load();
    // Load's second half (tests feed a document directly). Mounts the stripped sources too.
    bool LoadFrom( const json& InRegistry );
    // Back to a source tree: drops the registry and its mount (tests).
    void Reset();

    // True once a registry is loaded: sources are gone, so nothing may export (cook) again.
    bool IsCooked() const
    {
        return m_cooked.load( std::memory_order_acquire );
    }

    // The folded-in .meta JSON of an asset, or null.
    const json* FindMeta( const std::string& InLogicalPath ) const;
    // Where a model's texture reference resolved at cook time; empty when not recorded.
    std::string FindTextureReference( const std::string& InModel, const std::string& InReference ) const;
    // Every cooked asset's GUID (AssetDatabase registers them instead of scanning).
    const std::unordered_map<std::string, uint64_t>& GetGuids() const
    {
        return m_guids;
    }
    // The engine shader stages the cook shipped binaries for (the renderer prefetches these).
    const std::vector<std::string>& GetEngineShaderStages() const
    {
        return m_engineShaderStages;
    }
    const std::string& GetPlatform() const
    {
        return m_platform;
    }
    // Sprite atlases (ME-277): where a sprite image (its path) or a sheet's sprite ("<sheet>|<name>")
    // was packed: the atlas sheet and its sprite. Null when it wasn't.
    struct AtlasSprite
    {
        std::string Sheet;
        std::string Sprite;
    };
    const AtlasSprite* FindAtlasSprite( const std::string& InKey ) const;

private:
    CookedAssets() = default;

    std::atomic<bool> m_cooked{ false };
    std::string m_platform;
    std::unordered_map<std::string, json> m_metas;
    std::unordered_map<std::string, std::unordered_map<std::string, std::string>> m_textureReferences;
    std::unordered_map<std::string, uint64_t> m_guids;
    std::vector<std::string> m_engineShaderStages;
    std::unordered_map<std::string, AtlasSprite> m_atlasSprites;
};
