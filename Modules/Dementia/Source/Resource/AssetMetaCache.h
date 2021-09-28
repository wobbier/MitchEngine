#pragma once
#include "JSON.h"
#include "Singleton.h"
#include <Path.h>

#include "Pointers.h"

struct MetaBase;

class AssetMetaCache
{
	static constexpr char* kCachePath = ".tmp/MetadataCache.json";
public:
	AssetMetaCache();

	void Init();

	bool WasModified(const Path& filePath, SharedPtr<MetaBase> metaFile);
	void Update(const Path& filePath, SharedPtr<MetaBase> metaFile);

	void Load();
	void Save();

private:
	bool IsLoaded = false;

	struct CachedAssetInfo
	{
		long LastModified = 0;
	};

	std::map<std::string, CachedAssetInfo> m_cachedAssets;

	json Cache;
	ME_SINGLETON_DEFINITION(AssetMetaCache)
};