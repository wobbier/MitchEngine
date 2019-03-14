#pragma once
#include "ResourceCache.h"
#include <string>
#include "FilePath.h"

class Resource
{
	friend class ResourceCache;
public:
	Resource() = delete;
	bool IsCached() const;

	ResourceCache* GetResourceCache();

	const ResourceCache* GetResourceCache() const;

protected:
	Resource(const FilePath& path);
	virtual ~Resource();
	FilePath Path;

private:
	ResourceCache * Resources;
};
