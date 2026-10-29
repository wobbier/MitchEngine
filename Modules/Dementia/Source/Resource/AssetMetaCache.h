#pragma once
#include "JSON.h"
#include "Singleton.h"
#include <Path.h>

#include "Pointers.h"
#include <map>
#include <mutex>

struct MetaBase;

class AssetMetaCache
{
    static constexpr const char* kCachePath = ".tmp/MetadataCache.json";
public:
    AssetMetaCache();

    void Init();

    bool WasModified( const Path& filePath, SharedPtr<MetaBase> metaFile );
    void Update( const Path& filePath, SharedPtr<MetaBase> metaFile );

    void Load();
    void Save();
    // Shipped and cooked runs read the cache but never write it (they write nothing beside
    // themselves; ME-263).
    void SetReadOnly( bool InReadOnly ) { m_readOnly = InReadOnly; }

private:
    bool IsLoaded = false;
    bool m_readOnly = false;

    struct CachedAssetInfo
    {
        long LastModified = 0;
    };

    void SaveLocked();

    std::map<std::string, CachedAssetInfo> m_cachedAssets;
    std::mutex m_mutex;
    ME_SINGLETON_DEFINITION( AssetMetaCache )
};