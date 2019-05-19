#include "Resource.h"
#include "ResourceCache.h"

Resource::Resource(const FilePath& path)
	: Path(path)
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

const FilePath& Resource::GetPath() const
{
	return Path;
}
