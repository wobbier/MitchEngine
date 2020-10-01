#include <Resource/ResourceCache.h>

#include <Pointers.h>

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
	for (auto& iter = ResourceStack.begin(); iter != ResourceStack.end(); )
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
