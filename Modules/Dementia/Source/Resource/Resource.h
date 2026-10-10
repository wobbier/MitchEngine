#pragma once
#include <Path.h>

#include "Pointers.h"
#include <cstdint>

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

    // Async loading (ResourceCache::GetAsync). LoadAsync runs on a loader thread and must only do
    // CPU work (file IO, decoding): no GPU calls, no engine state. FinishAsyncLoad then completes
    // the load on the main thread (GPU upload). Types that don't override these load with Load().
    virtual bool SupportsAsyncLoad() const;
    virtual bool LoadAsync();
    virtual bool FinishAsyncLoad();

    // GetAsync caches a resource at once; it's usable when it's no longer loading.
    bool IsLoading() const;
    bool HasLoadFailed() const;

protected:
    Resource( const Path& path );
    virtual ~Resource();
    Path FilePath;
    SharedPtr<MetaBase> Metadata = nullptr;

private:
    void SetMetadata( SharedPtr<MetaBase> metadata );

    ResourceCache* Resources = nullptr;
    std::size_t ResourceType;
    // Written on the main thread only (the loader threads only run LoadAsync).
    enum class LoadState : uint8_t
    {
        Ready,
        Loading,
        Failed
    };
    LoadState m_loadState = LoadState::Ready;
};
