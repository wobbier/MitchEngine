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
#include "AssetDatabase.h"
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
//
// GetAsync loads types that support it (textures) on loader threads; the main thread finishes
// them (GPU upload) in PumpAsyncLoads, which the engine calls every frame before rendering.
class ResourceCache
{
    ResourceCache();
    ~ResourceCache();

public:
    std::size_t GetCacheSize() const;

    template<class T, typename... Args>
    SharedPtr<T> Get( const Path& InFilePath, Args&& ... args );

    // Like Get, but types that support it load on a background thread: the resource is cached and
    // returned at once, still loading (Resource::IsLoading), and becomes usable on a later frame
    // when PumpAsyncLoads finishes it. A Get of a resource that is still loading finishes it on
    // the spot, so Get always returns loaded resources.
    template<class T, typename... Args>
    SharedPtr<T> GetAsync( const Path& InFilePath, Args&& ... args );

    // Main thread: finishes the async loads whose background part is done, for up to InBudgetMs
    // (always at least one). Returns how many finished.
    int PumpAsyncLoads( double InBudgetMs = 2.0 );
    // Finishes every async load, helping with the queued ones (loading screens, deterministic runs).
    void WaitForAsyncLoads();
    // Finishes this resource's async load now (no-op unless it's loading).
    void CompleteLoad( const SharedPtr<Resource>& InResource );
    // Async loads not finished yet.
    std::size_t GetPendingLoadCount() const;

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

    // Drops every cache reference and cancels the async loads (shutdown / forced refresh).
    void ReleaseAll();

    // Re-exports (tools builds) and reloads cached resources affected by the changed files. Slow
    // cooks (MetaBase::ExportsInBackground: shaders, textures, models) run on reimport threads and
    // reload later from PumpReimports; quick ones reload now. Returns the paths reloaded now.
    std::vector<std::string> OnFilesChanged( const std::vector<std::string>& InChangedFullPaths );
    // Main thread, once a frame: reloads the resources whose background re-export finished (a file
    // changed again meanwhile is re-exported first). Returns their paths.
    std::vector<std::string> PumpReimports();
    // Blocks until every background reimport is done, then reloads them.
    std::vector<std::string> WaitForReimports();
    // Background re-exports queued or running.
    std::size_t GetPendingReimportCount() const;

    SharedPtr<MetaBase> LoadMetadata( const Path& filePath );

private:
    template<class T, typename... Args>
    SharedPtr<T> Load( const Path& InFilePath, bool InAsync, Args&& ... args );
    void ExportIfNeeded( const Path& InFilePath, const SharedPtr<MetaBase>& InMetaFile, bool InForce );
    // The background part of an async load: a deferred first-time cook, then Resource::LoadAsync.
    static bool RunBackgroundLoad( Resource& InResource );
    void QueueAsyncLoad( const SharedPtr<Resource>& InResource );
    void FinishAsyncLoad( const SharedPtr<Resource>& InResource, bool InBackgroundSucceeded );
    // Cancels the queued loads and joins the loader threads.
    void StopAsyncLoads();
    void StopReimports();
    void QueueReimport( const std::string& InPath, const SharedPtr<Resource>& InResource, const SharedPtr<MetaBase>& InMeta );
    // Saves the meta and reloads a resource whose re-export finished.
    void FinishReimport( const std::string& InPath, const SharedPtr<Resource>& InResource, const SharedPtr<MetaBase>& InMeta );

    // Loader threads and their queues (ResourceCache.cpp).
    struct AsyncLoads;
    std::unique_ptr<AsyncLoads> m_async;
    // Reimport threads (hot reload of slow cooks).
    struct Reimports;
    std::unique_ptr<Reimports> m_reimports;

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
    return Load<T>( InFilePath, false, std::forward<Args>( args )... );
}


template<class T, typename... Args>
SharedPtr<T> ResourceCache::GetAsync( const Path& InFilePath, Args&& ... args )
{
    return Load<T>( InFilePath, true, std::forward<Args>( args )... );
}


template<class T, typename... Args>
SharedPtr<T> ResourceCache::Load( const Path& InFilePath, bool InAsync, Args&& ... args )
{
    std::unique_lock<std::recursive_mutex> lock( m_mutex );
    auto I = m_resourceStack.find( InFilePath.FullPath );
    if( I != m_resourceStack.end() )
    {
        SharedPtr<T> Res = std::dynamic_pointer_cast<T>( I->second );
        if( !Res )
        {
            YIKES( "ResourceCache: " + InFilePath.FullPath + " is cached as a different resource type." );
        }
        else if( !InAsync && Res->IsLoading() )
        {
            lock.unlock();
            CompleteLoad( Res );
        }
        return Res;
    }

    if( !InFilePath.Exists )
    {
        // Moved in the editor this session: references to the old path still load.
        const std::string moved = AssetDatabase::Get().FindMovedPath( InFilePath.GetLocalPathString() );
        if( !moved.empty() )
        {
            lock.unlock();
            return Load<T>( Path( moved ), InAsync, std::forward<Args>( args )... );
        }
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

    // An asset loaded in the background is also cooked there the first time (a slow compiler run:
    // texturec, the Assimp export), so a scene full of new assets doesn't freeze the main thread.
    bool exportInBackground = false;
#if USING( ME_TOOLS )
    exportInBackground = InAsync && InFilePath.Exists && metaFile && metaFile->ExportsInBackground()
        && ( metaFile->FlaggedForExport || !compiledFileExists );
#endif
    if( !exportInBackground )
    {
        ExportIfNeeded( InFilePath, metaFile, !compiledFileExists );
    }

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
    if( InAsync && Res->SupportsAsyncLoad() )
    {
        Res->m_loadState = Resource::LoadState::Loading;
        if( exportInBackground )
        {
            Res->m_pendingExport = metaFile;
        }
        m_resourceStack[InFilePath.FullPath] = Res;
        QueueAsyncLoad( Res );
        return Res;
    }
    if( exportInBackground )
    {
        ExportIfNeeded( InFilePath, metaFile, true );   // this type loads synchronously after all
    }
    if( !Res->Load() )
    {
        YIKES( "Resource failed to load: " + InFilePath.FullPath );
        Res->m_loadState = Resource::LoadState::Failed;
    }
    m_resourceStack[InFilePath.FullPath] = Res;
    return Res;
}
