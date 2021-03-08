#include <Resource/ResourceCache.h>

#include <Pointers.h>
#include "MetaRegistry.h"
#include "JSON.h"
#include "File.h"

ResourceCache::ResourceCache()
{
}

ResourceCache::~ResourceCache()
{
	for (auto I : ResourceStack)
	{
		if (I.second)
		{
			I.second->Resources = nullptr;
			I.second.reset();
		}
	}
	ResourceStack.clear();
}

std::size_t ResourceCache::GetCacheSize() const
{
	return ResourceStack.size();
}

void ResourceCache::TryToDestroy(Resource* resource)
{
	std::map<std::string, std::shared_ptr<Resource>>::iterator I;
	I = ResourceStack.find(resource->FilePath.FullPath);
	if (I != ResourceStack.end())
	{
		if (I->second.use_count() == 1)
		{
			ResourceStack.erase(I);
		}
		return;
	}
}

const std::map<std::string, std::shared_ptr<Resource>>& ResourceCache::GetResouceStack() const
{
	return ResourceStack;
}

void ResourceCache::Dump()
{
	for (std::map<std::string, std::shared_ptr<Resource>>::iterator iter = ResourceStack.begin(); iter != ResourceStack.end(); )
	{
		if (iter->second.use_count() == 1)
		{
			iter = ResourceStack.erase(iter);
		}
		else
		{
			++iter;
		}
	}
}

MetaBase* ResourceCache::LoadMetadata(const Path& filePath)
{
	MetaBase* metadata = nullptr;
	MetaRegistry::iterator it = GetMetadatabase().reg.find(filePath.Extension);
	if (it != GetMetadatabase().reg.end())
	{
		Path metaPath = Path(filePath.FullPath + ".meta");
		File metaFile = File(metaPath);
		json j;

		metadata = it->second(filePath);

		if (metaPath.Exists)
		{
			j = json::parse(metaFile.Read());
			metadata->Deserialize(j);
		}
		else
		{
			metadata->Serialize(j);
			metaFile.Write(j.dump(4));
			metadata->FlaggedForExport = true;
		}
	}
	return metadata;
}
