#pragma once
#include "ResourceCache.h"
#include <string>

class Resource
{
	friend class ResourceCache;
public:
	Resource() = default;
	virtual ~Resource() = default;

	bool IsCached() const
	{
		return Resources != nullptr;
	}

	ResourceCache* GetResourceCache()
	{
		return Resources;
	}

	const ResourceCache* GetResourceCache() const
	{
		return Resources;
	}

private:
	ResourceCache * Resources;
};
