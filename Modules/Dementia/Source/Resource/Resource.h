#pragma once
#include <Path.h>

class ResourceCache;

class Resource
{
	friend class ResourceCache;
public:
	Resource() = delete;

	bool IsCached() const;

	ResourceCache* GetResourceCache();
	const ResourceCache* GetResourceCache() const;

	const Path& GetPath() const;
	const std::size_t GetResourceType() const;

protected:
	Resource(const Path& path);
	virtual ~Resource();
	Path FilePath;

private:
	ResourceCache* Resources = nullptr;
	std::size_t ResourceType;
};
