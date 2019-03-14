#include "ResourceCache.h"

ResourceCache::ResourceCache()
{
	Push();
}

ResourceCache::~ResourceCache()
{
	Pop();
}

void ResourceCache::Push()
{
	ResourceStack.push_back(std::map<std::string, std::shared_ptr<Resource>>());
}

void ResourceCache::Pop()
{
	if (ResourceStack.empty())
	{
		return;
	}

	auto& V = ResourceStack[ResourceStack.size() - 1];
	for (auto I = V.begin(); I != V.end(); ++I)
	{
		I->second.reset();
	}
	ResourceStack.pop_back();
}

size_t ResourceCache::GetStackSize()
{
	return ResourceStack.size();
}

void ResourceCache::TryToDestroy(Resource* resource)
{
	std::map<std::string, std::shared_ptr<Resource>>::iterator I;
	for (std::size_t i = ResourceStack.size() - 1; i >= 0; i--)
	{
		I = ResourceStack[i].find(resource->Path.FullPath);
		if (I != ResourceStack[i].end())
		{
			ResourceStack[i].erase(I);
			return;
		}
	}
}
