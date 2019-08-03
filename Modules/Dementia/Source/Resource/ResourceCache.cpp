#include "ResourceCache.h"
#include "Pointers.h"

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

size_t ResourceCache::GetCacheSize()
{
	return ResourceStack.size();
}

void ResourceCache::TryToDestroy(Resource* resource)
{
	std::map<std::string, std::shared_ptr<Resource>>::iterator I;
	I = ResourceStack.find(resource->FilePath.FullPath);
	if (I != ResourceStack.end())
	{
		ResourceStack.erase(I);
		return;
	}
}
