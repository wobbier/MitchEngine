#include <Resource/Resource.h>

#include <Resource/ResourceCache.h>

Resource::Resource(const Path& path)
	: FilePath(path)
{
}

Resource::~Resource()
{
	Metadata.reset();
	Metadata = nullptr;
}

void Resource::SetMetadata(SharedPtr<MetaBase> metadata)
{
	Metadata = metadata;
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

SharedPtr<MetaBase> Resource::GetMetadata()
{
	return Metadata;
}

void Resource::Load()
{

}

void Resource::Reload()
{

}
