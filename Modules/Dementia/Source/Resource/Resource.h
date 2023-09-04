#pragma once
#include <Path.h>

#include "Pointers.h"

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

    SharedPtr<MetaBase> GetMetadata();

    virtual bool Load();
    virtual void Reload();

protected:
    Resource( const Path& path );
    virtual ~Resource();
    Path FilePath;
    SharedPtr<MetaBase> Metadata = nullptr;

private:
    void SetMetadata( SharedPtr<MetaBase> metadata );

    ResourceCache* Resources = nullptr;
    std::size_t ResourceType;
};
