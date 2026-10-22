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

private:
    bool IsLoaded = false;

    struct CachedAssetInfo
    {
        long LastModified = 0;
    };

    void SaveLocked();

    std::map<std::string, CachedAssetInfo> m_cachedAssets;
    std::mutex m_mutex;
    ME_SINGLETON_DEFINITION( AssetMetaCache )
};