#pragma once
#include <Path.h>

class ResourceCache;
struct MetaBase;

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

	MetaBase* GetMetadata();

	virtual void Load();

protected:
	Resource(const Path& path);
	virtual ~Resource();
	Path FilePath;
	MetaBase* Metadata = nullptr;

private:
	void SetMetadata(MetaBase* metadata);

	ResourceCache* Resources = nullptr;
	std::size_t ResourceType;
};
