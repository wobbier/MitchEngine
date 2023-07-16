#include "AssetMetaCache.h"
#include "Utils/PlatformUtils.h"
#include <File.h>
#include <string>
#include "MetaFile.h"
#include <CLog.h>
#include <sys/stat.h>

AssetMetaCache::AssetMetaCache()
{
	Path gameSpecificCnfigPath(kCachePath);

	if (!gameSpecificCnfigPath.Exists)
	{
		PlatformUtils::CreateDirectory(gameSpecificCnfigPath);
		File gameEngineCfg = File(gameSpecificCnfigPath);
		gameEngineCfg.Write("{}");
	}
}

void AssetMetaCache::Init()
{

}

bool AssetMetaCache::WasModified(const Path& filePath, SharedPtr<MetaBase> metaFile)
{
	bool wasModified = true;
	if (m_cachedAssets.find(filePath.GetLocalPath().data()) != m_cachedAssets.end())
	{
		wasModified = m_cachedAssets[filePath.GetLocalPath().data()].LastModified != metaFile->LastModified;
	}
	return wasModified;
}

void AssetMetaCache::Update(const Path& filePath, SharedPtr<MetaBase> metaFile)
{
	if (!metaFile)
	{
		YIKES("[AssetMetaCache] Trying to update a null MetaBase");
	}

	struct stat fileInfo;
	if (stat(filePath.FullPath.c_str(), &fileInfo) != 0)
	{
		// Use stat() to get the info
		//std::cerr << "Error: " << strerror(errno) << '\n';
	}

	metaFile->LastModified = static_cast<long>(fileInfo.st_mtime);

	if (m_cachedAssets.find(filePath.GetLocalPath().data()) != m_cachedAssets.end())
	{
		m_cachedAssets[filePath.GetLocalPath().data()].LastModified = static_cast<long>(fileInfo.st_mtime);
	}
	else
	{
		CachedAssetInfo info;
		info.LastModified = static_cast<long>(fileInfo.st_mtime);
		m_cachedAssets[filePath.GetLocalPath().data()] = info;
	}

	Save();
}

void AssetMetaCache::Load()
{
	if (!IsLoaded)
	{
		Path configPath(kCachePath);
		File cacheFile(configPath);
		cacheFile.Read();
		if (cacheFile.Data.length() > 0)
		{
			Cache = json::parse(cacheFile.Data);
			for (auto i : Cache)
			{
				CachedAssetInfo ent;
				ent.LastModified = i["LastModified"];
				m_cachedAssets[i["Key"]] = ent;
			}
		}
		IsLoaded = true;
	}
}

void AssetMetaCache::Save()
{
	json j_map;
	for (auto& thing : m_cachedAssets)
	{
		json j;
		j["Key"] = thing.first;
		j["LastModified"] = thing.second.LastModified;
		j_map.push_back(j);
	}
	Path configPath(kCachePath);
	File cacheFile(configPath);
	cacheFile.Write(j_map.dump(4));
}

