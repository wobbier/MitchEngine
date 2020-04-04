#include "Resource.h"
#include "ResourceCache.h"

Resource::Resource(const Path& path)
	: FilePath(path)
{
}

Resource::~Resource()
{
	if (Resources)
	{
		Resources->TryToDestroy(this);
	}
}

bool Resource::IsCached() const
{
	return Resources != nullptr;
}

ResourceCache* Resource::GetResourceCache()
{
	return Resources;
}

const ResourceCache* Resource::GetResourceCache() const
{
	return Resources;
}

const Path& Resource::GetPath() const
{
	return FilePath;
}

const std::size_t Resource::GetResourceType() const
{
	return ResourceType;
}
