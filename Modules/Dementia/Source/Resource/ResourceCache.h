#pragma once
#include <map>
#include <string>
#include <Resource/Resource.h>

#include <ClassTypeId.h>
#include <CLog.h>
#include <Path.h>
#include <Pointers.h>
#include <Singleton.h>
#include "MetaFile.h"
#include "AssetMetaCache.h"
#include <memory>
#include <mutex>
#include <vector>

class Resource;

typedef const std::map<std::string, std::shared_ptr<Resource>> ResourceStack;

// Path-keyed cache of loaded resources (thread safe).
//
// Resources nobody references any more are kept alive for KeepAliveSeconds before being evicted,
// so assets that are briefly unused (scene reloads, prefab spawns, editor previews) aren't reloaded
// from disk. In tools builds, OnFilesChanged re-exports and hot reloads changed assets.
class ResourceCache
{
    ResourceCache();
    ~ResourceCache();

public:
    std::size_t GetCacheSize() const;

    template<class T, typename... Args>
    SharedPtr<T> Get( const Path& InFilePath, Args&& ... args );

    SharedPtr<Resource> GetCached( const Path& InFilePath );
#if USING( ME_TOOLS )
    Path FindByName( const Path& inRoot, const std::string& InFileName );
#endif

    void TryToDestroy( Resource* resource );
    // Drops the cache's reference to a resource even while others still hold it (the next Get
    // loads it afresh).
    void Evict( const std::string& InFullPath );

    ResourceStack& GetResouceStack() const;

    // Evicts resources that have been unreferenced for longer than the keep-alive time.
    // Cheap to call every frame (does the work a few times a second).
    void Dump();
    void SetKeepAliveSeconds( float InSeconds );

    // Drops every cache reference (shutdown / forced refresh).
    void ReleaseAll();

    // Re-exports (tools builds) and reloads cached resources affected by the changed files.
    // Returns the paths that were reloaded.
    std::vector<std::string> OnFilesChanged( const std::vector<std::string>& InChangedFullPaths );

    SharedPtr<MetaBase> LoadMetadata( const Path& filePath );

private:
    void ExportIfNeeded( const Path& InFilePath, const SharedPtr<MetaBase>& InMetaFile, bool InForce );

    std::map<std::string, std::shared_ptr<Resource>> m_resourceStack;
    // Seconds (steady clock) at which each entry became unreferenced; absent while referenced.
    std::map<std::string, double> m_unreferencedSince;
    float m_keepAliveSeconds = 15.f;
    double m_lastCollectTime = 0.0;
    mutable std::recursive_mutex m_mutex;

    ME_SINGLETON_DEFINITION( ResourceCache )
};

template<class T, typename... Args>
SharedPtr<T> ResourceCache::Get( const Path& InFilePath, Args&& ... args )
{
    std::lock_guard<std::recursive_mutex> lock( m_mutex );
    auto I = m_resourceStack.find( InFilePath.FullPath );
    if( I != m_resourceStack.end() )
    {
        SharedPtr<T> Res = std::dynamic_pointer_cast<T>( I->second );
        if( !Res )
        {
            YIKES( "ResourceCache: " + InFilePath.FullPath + " is cached as a different resource type." );
        }
        return Res;
    }

    SharedPtr<MetaBase> metaFile = LoadMetadata( InFilePath );

    bool compiledFileExists = true;
    // Pass-through assets (wav, mp3, mat: the "compiled" twin has the source's own extension) have
    // nothing to cook, so a missing twin doesn't force an export.
    if( metaFile && metaFile->GetExtension2() != InFilePath.GetExtension() )
    {
        Path compiledAsset = Path( metaFile->FilePath.FullPath + "." + metaFile->GetExtension2() );
        compiledFileExists = compiledAsset.Exists;
    }

    ExportIfNeeded( InFilePath, metaFile, !compiledFileExists );

    if( !InFilePath.Exists && !compiledFileExists && metaFile && !metaFile->FlaggedForExport )
    {
        YIKES( "Failed to load resource: " + InFilePath.FullPath );
        return {};
    }

    if( !InFilePath.Exists )
    {
        BRUH( "Resource source file is missing (using compiled data if present): " + InFilePath.FullPath );
    }

    SharedPtr<T> Res = MakeShared<T>( InFilePath, std::forward<Args>( args )... );
    Res->Resources = this;
    TypeId id = ClassTypeId<Resource>::GetTypeId<T>();
    Res->ResourceType = static_cast<std::size_t>( id );
    Res->SetMetadata( metaFile );
    if( !Res->Load() )
    {
        YIKES( "Resource failed to load: " + InFilePath.FullPath );
    }
    m_resourceStack[InFilePath.FullPath] = Res;
    return Res;
}
