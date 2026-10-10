#include <Resource/ResourceCache.h>

#include <Pointers.h>
#include "MetaRegistry.h"
#include "JSON.h"
#include "File.h"
#include "AssetMetaCache.h"
#include <optional>
#include <chrono>
#include <algorithm>
#include <condition_variable>
#include <deque>
#include <unordered_map>
#include <unordered_set>
#include <thread>
#include "optick.h"


struct ResourceCache::AsyncLoads
{
    std::mutex Mutex;
    std::condition_variable WorkReady;  // loader threads wait for queued loads
    std::condition_variable WorkDone;   // the main thread waits for background parts
    std::deque<SharedPtr<Resource>> Queued;
    std::vector<Resource*> Running;     // in LoadAsync on a loader thread
    std::deque<std::pair<SharedPtr<Resource>, bool>> Done;  // background part done (and whether it worked)
    std::vector<std::thread> Threads;
    bool Stopping = false;
    std::unordered_map<Resource*, uint64_t> Sequence;   // request order of the pending loads
    uint64_t NextSequence = 0;

    void Run( int InIndex )
    {
        const std::string threadName = "Resource Loader " + std::to_string( InIndex );
        OPTICK_THREAD( threadName.c_str() );
        std::unique_lock<std::mutex> lock( Mutex );
        while( true )
        {
            WorkReady.wait( lock, [this] { return Stopping || !Queued.empty(); } );
            if( Stopping )
            {
                return;
            }
            SharedPtr<Resource> resource = std::move( Queued.front() );
            Queued.pop_front();
            Running.push_back( resource.get() );
            lock.unlock();
            const bool succeeded = ResourceCache::RunBackgroundLoad( *resource );
            lock.lock();
            Running.erase( std::find( Running.begin(), Running.end(), resource.get() ) );
            Done.emplace_back( std::move( resource ), succeeded );
            WorkDone.notify_all();
        }
    }
};


#if USING( ME_TOOLS )


class PathResolver
{
public:
    static std::optional<std::filesystem::path> FindMatchingFile( const std::filesystem::path& filename, const std::filesystem::path& rootDir )
    {
        for( const auto& entry : std::filesystem::recursive_directory_iterator( rootDir ) )
        {
            if( entry.is_regular_file() && entry.path().filename() == filename )
            {
                return entry.path();
            }
        }
        return std::nullopt;
    }


    static std::filesystem::path ResolvePath( const std::filesystem::path& requestedPath, const std::filesystem::path& assetRoot )
    {
        std::filesystem::path filename = requestedPath.filename();

        if( auto foundFile = FindMatchingFile( filename, assetRoot ) )
        {
            return *foundFile;
        }

        return requestedPath;
    }
};


Path ResourceCache::FindByName( const Path& inRoot, const std::string& InFileName )
{
    std::filesystem::path inputPath = InFileName;
    std::filesystem::path assetRoot = inRoot.FullPath;

    return Path(PathResolver::ResolvePath( inputPath, assetRoot ).generic_string());
}


#endif


struct ResourceCache::Reimports
{
    struct Job
    {
        std::string Path;
        SharedPtr<Resource> Target;
        SharedPtr<MetaBase> Meta;
    };
    mutable std::mutex Mutex;
    std::condition_variable WorkReady;
    std::condition_variable WorkDone;
    std::deque<Job> Queued;
    std::vector<std::string> Running;
    std::deque<Job> Done;
    std::unordered_set<std::string> Rerun;  // changed again while it was compiling
    std::vector<std::thread> Threads;
    bool Stopping = false;

    bool IsPending( const std::string& InPath ) const
    {
        return std::find( Running.begin(), Running.end(), InPath ) != Running.end()
            || std::any_of( Queued.begin(), Queued.end(), [&InPath]( const Job& InJob ) { return InJob.Path == InPath; } );
    }

    void Run( int InIndex )
    {
        const std::string threadName = "Reimport " + std::to_string( InIndex );
        OPTICK_THREAD( threadName.c_str() );
        std::unique_lock<std::mutex> lock( Mutex );
        while( true )
        {
            WorkReady.wait( lock, [this] { return Stopping || !Queued.empty(); } );
            if( Stopping )
            {
                return;
            }
            Job job = std::move( Queued.front() );
            Queued.pop_front();
            Running.push_back( job.Path );
            lock.unlock();
            {
                OPTICK_EVENT( "MetaBase::Export" );
                job.Meta->Export();
            }
            lock.lock();
            Running.erase( std::find( Running.begin(), Running.end(), job.Path ) );
            Done.push_back( std::move( job ) );
            WorkDone.notify_all();
        }
    }
};


ResourceCache::ResourceCache()
    : m_async( std::make_unique<AsyncLoads>() )
    , m_reimports( std::make_unique<Reimports>() )
{
    AssetMetaCache::GetInstance().Load();
}

ResourceCache::~ResourceCache()
{
    StopAsyncLoads();
    StopReimports();
    AssetMetaCache::GetInstance().Save();
    for( auto i : m_resourceStack )
    {
        if( i.second )
        {
            i.second->Resources = nullptr;
            i.second.reset();
        }
    }
    m_resourceStack.clear();
}

std::size_t ResourceCache::GetCacheSize() const
{
    std::lock_guard<std::recursive_mutex> lock( m_mutex );
    return m_resourceStack.size();
}

SharedPtr<Resource> ResourceCache::GetCached( const Path& InFilePath )
{
    std::lock_guard<std::recursive_mutex> lock( m_mutex );
    auto i = m_resourceStack.find( InFilePath.FullPath );
    if( i != m_resourceStack.end() )
    {
        return i->second;
    }
    return {};
}

void ResourceCache::TryToDestroy( Resource* resource )
{
    std::lock_guard<std::recursive_mutex> lock( m_mutex );
    std::map<std::string, std::shared_ptr<Resource>>::iterator I;
    I = m_resourceStack.find( resource->FilePath.FullPath );
    if( I != m_resourceStack.end() )
    {
        if( I->second.use_count() == 1 )
        {
            m_resourceStack.erase( I );
        }
        return;
    }
}

void ResourceCache::Evict( const std::string& InFullPath )
{
    std::lock_guard<std::recursive_mutex> lock( m_mutex );
    auto entry = m_resourceStack.find( InFullPath );
    if( entry != m_resourceStack.end() )
    {
        if( entry->second )
        {
            entry->second->Resources = nullptr;
        }
        m_resourceStack.erase( entry );
    }
    m_unreferencedSince.erase( InFullPath );
}


ResourceStack& ResourceCache::GetResouceStack() const
{
    // Callers iterate on the main thread; loads from other threads take the lock.
    return m_resourceStack;
}

void ResourceCache::Dump()
{
    const double now = std::chrono::duration<double>( std::chrono::steady_clock::now().time_since_epoch() ).count();
    std::lock_guard<std::recursive_mutex> lock( m_mutex );
    if( now - m_lastCollectTime < 0.25 )
    {
        return;
    }
    m_lastCollectTime = now;

    for( auto iter = m_resourceStack.begin(); iter != m_resourceStack.end(); )
    {
        if( iter->second.use_count() == 1 )
        {
            auto [since, inserted] = m_unreferencedSince.try_emplace( iter->first, now );
            if( !inserted && now - since->second >= m_keepAliveSeconds )
            {
                m_unreferencedSince.erase( since );
                iter = m_resourceStack.erase( iter );
                continue;
            }
        }
        else
        {
            m_unreferencedSince.erase( iter->first );
        }
        ++iter;
    }
}


void ResourceCache::SetKeepAliveSeconds( float InSeconds )
{
    std::lock_guard<std::recursive_mutex> lock( m_mutex );
    m_keepAliveSeconds = InSeconds < 0.f ? 0.f : InSeconds;
}


void ResourceCache::QueueAsyncLoad( const SharedPtr<Resource>& InResource )
{
    {
        std::lock_guard<std::mutex> lock( m_async->Mutex );
        if( m_async->Threads.empty() )
        {
            // IO and decoding: a few threads, separate from the job workers so frame jobs never
            // wait behind a file read.
            const int count = static_cast<int>( std::clamp( std::thread::hardware_concurrency() / 4u, 1u, 4u ) );
            for( int i = 0; i < count; ++i )
            {
                m_async->Threads.emplace_back( [this, i] { m_async->Run( i ); } );
            }
        }
        m_async->Queued.push_back( InResource );
        m_async->Sequence[InResource.get()] = m_async->NextSequence++;
    }
    m_async->WorkReady.notify_one();
}


bool ResourceCache::RunBackgroundLoad( Resource& InResource )
{
#if USING( ME_TOOLS )
    if( InResource.m_pendingExport )
    {
        OPTICK_EVENT( "MetaBase::Export" );
        CLog::Log( CLog::LogType::Info, "Exporting asset: " + InResource.FilePath.FullPath );
        InResource.m_pendingExport->Export();
        InResource.m_pendingExport->Save();
    }
#endif
    OPTICK_EVENT( "Resource::LoadAsync" );
    return InResource.LoadAsync();
}


void ResourceCache::FinishAsyncLoad( const SharedPtr<Resource>& InResource, bool InBackgroundSucceeded )
{
    OPTICK_EVENT( "Resource::FinishAsyncLoad" );
#if USING( ME_TOOLS )
    if( InResource->m_pendingExport )
    {
        AssetMetaCache::GetInstance().Update( InResource->FilePath, InResource->m_pendingExport );
        InResource->m_pendingExport.reset();
    }
#endif
    {
        std::lock_guard<std::mutex> lock( m_async->Mutex );
        m_async->Sequence.erase( InResource.get() );
    }
    const bool succeeded = InBackgroundSucceeded && InResource->FinishAsyncLoad();
    InResource->m_loadState = succeeded ? Resource::LoadState::Ready : Resource::LoadState::Failed;
    if( !succeeded )
    {
        YIKES( "Resource failed to load: " + InResource->GetPath().FullPath );
    }
}


int ResourceCache::PumpAsyncLoads( double InBudgetMs )
{
    OPTICK_EVENT( "ResourceCache::PumpAsyncLoads" );
    const auto start = std::chrono::steady_clock::now();
    int finished = 0;
    while( true )
    {
        std::pair<SharedPtr<Resource>, bool> next;
        {
            std::lock_guard<std::mutex> lock( m_async->Mutex );
            if( m_async->Done.empty() )
            {
                break;
            }
            next = std::move( m_async->Done.front() );
            m_async->Done.pop_front();
        }
        FinishAsyncLoad( next.first, next.second );
        ++finished;
        if( std::chrono::duration<double, std::milli>( std::chrono::steady_clock::now() - start ).count() >= InBudgetMs )
        {
            break;
        }
    }
    return finished;
}


void ResourceCache::WaitForAsyncLoads()
{
    OPTICK_EVENT( "ResourceCache::WaitForAsyncLoads" );
    // Every background part first (helping with the queued ones), then the main-thread parts in
    // request order: GPU objects are created in the same order every run, which keeps
    // deterministic captures exact (texture handles feed batch keys and draw order).
    std::vector<std::pair<SharedPtr<Resource>, bool>> finished;
    while( true )
    {
        SharedPtr<Resource> queued;
        {
            std::unique_lock<std::mutex> lock( m_async->Mutex );
            if( !m_async->Queued.empty() )
            {
                queued = std::move( m_async->Queued.front() );
                m_async->Queued.pop_front();
            }
            else if( !m_async->Running.empty() )
            {
                m_async->WorkDone.wait( lock, [this] { return m_async->Running.empty() || !m_async->Queued.empty(); } );
                continue;
            }
            else
            {
                for( auto& done : m_async->Done )
                {
                    finished.push_back( std::move( done ) );
                }
                m_async->Done.clear();
                break;
            }
        }
        const bool succeeded = RunBackgroundLoad( *queued );
        finished.emplace_back( std::move( queued ), succeeded );
    }
    {
        std::lock_guard<std::mutex> lock( m_async->Mutex );
        std::sort( finished.begin(), finished.end(), [this]( const auto& InA, const auto& InB ) {
            return m_async->Sequence[InA.first.get()] < m_async->Sequence[InB.first.get()];
        } );
    }
    for( auto& [resource, succeeded] : finished )
    {
        FinishAsyncLoad( resource, succeeded );
    }
}


void ResourceCache::CompleteLoad( const SharedPtr<Resource>& InResource )
{
    if( !InResource || !InResource->IsLoading() )
    {
        return;
    }
    bool backgroundSucceeded = false;
    {
        std::unique_lock<std::mutex> lock( m_async->Mutex );
        auto queued = std::find( m_async->Queued.begin(), m_async->Queued.end(), InResource );
        if( queued != m_async->Queued.end() )
        {
            // Not started yet: do it here instead of waiting for a loader thread.
            m_async->Queued.erase( queued );
            lock.unlock();
            backgroundSucceeded = RunBackgroundLoad( *InResource );
        }
        else
        {
            m_async->WorkDone.wait( lock, [this, &InResource] { return std::find( m_async->Running.begin(), m_async->Running.end(), InResource.get() ) == m_async->Running.end(); } );
            auto done = std::find_if( m_async->Done.begin(), m_async->Done.end(), [&InResource]( const auto& InEntry ) { return InEntry.first == InResource; } );
            if( done == m_async->Done.end() )
            {
                return;
            }
            backgroundSucceeded = done->second;
            m_async->Done.erase( done );
        }
    }
    FinishAsyncLoad( InResource, backgroundSucceeded );
}


std::size_t ResourceCache::GetPendingLoadCount() const
{
    std::lock_guard<std::mutex> lock( m_async->Mutex );
    return m_async->Queued.size() + m_async->Running.size() + m_async->Done.size();
}


void ResourceCache::StopAsyncLoads()
{
    std::vector<std::thread> threads;
    {
        std::lock_guard<std::mutex> lock( m_async->Mutex );
        m_async->Stopping = true;
        threads.swap( m_async->Threads );
    }
    m_async->WorkReady.notify_all();
    for( std::thread& thread : threads )
    {
        thread.join();
    }
    std::lock_guard<std::mutex> lock( m_async->Mutex );
    for( const SharedPtr<Resource>& resource : m_async->Queued )
    {
        resource->m_loadState = Resource::LoadState::Failed;
    }
    for( const auto& done : m_async->Done )
    {
        done.first->m_loadState = Resource::LoadState::Failed;
    }
    m_async->Queued.clear();
    m_async->Done.clear();
    m_async->Sequence.clear();
    m_async->Stopping = false;
}


void ResourceCache::ReleaseAll()
{
    StopAsyncLoads();
    StopReimports();
    std::lock_guard<std::recursive_mutex> lock( m_mutex );
    for( auto& entry : m_resourceStack )
    {
        if( entry.second )
        {
            entry.second->Resources = nullptr;
        }
    }
    m_resourceStack.clear();
    m_unreferencedSince.clear();
}


void ResourceCache::ExportIfNeeded( const Path& InFilePath, const SharedPtr<MetaBase>& InMetaFile, bool InForce )
{
#if USING( ME_TOOLS )
    if( InMetaFile && ( InMetaFile->FlaggedForExport || InForce ) )
    {
        CLog::Log( CLog::LogType::Info, "Exporting asset: " + InFilePath.FullPath );
        InMetaFile->Export();
        InMetaFile->Save();
        AssetMetaCache::GetInstance().Update( InFilePath, InMetaFile );
    }
#endif
}


std::vector<std::string> ResourceCache::OnFilesChanged( const std::vector<std::string>& InChangedFullPaths )
{
    std::vector<std::string> reloaded;
    for( const std::string& changed : InChangedFullPaths )
    {
        // Editing import settings (.meta) reimports the asset it describes.
        std::string sourcePath = changed;
        const std::string metaSuffix = ".meta";
        if( sourcePath.size() > metaSuffix.size() && sourcePath.compare( sourcePath.size() - metaSuffix.size(), metaSuffix.size(), metaSuffix ) == 0 )
        {
            sourcePath.resize( sourcePath.size() - metaSuffix.size() );
        }

        SharedPtr<Resource> resource;
        {
            std::lock_guard<std::recursive_mutex> lock( m_mutex );
            auto found = m_resourceStack.find( sourcePath );
            if( found != m_resourceStack.end() )
            {
                resource = found->second;
            }
        }
        if( !resource )
        {
            continue;
        }
        CompleteLoad( resource );

#if USING( ME_TOOLS )
        Path sourceFile( sourcePath );
        if( SharedPtr<MetaBase> metaFile = LoadMetadata( sourceFile ) )
        {
            if( metaFile->ExportsInBackground() )
            {
                // Recompile off the main thread; PumpReimports reloads it when that's done.
                QueueReimport( sourcePath, resource, metaFile );
                continue;
            }
            ExportIfNeeded( sourceFile, metaFile, true );
            resource->SetMetadata( metaFile );
        }
#endif
        resource->Reload();
        reloaded.push_back( sourcePath );
    }
    return reloaded;
}


void ResourceCache::QueueReimport( const std::string& InPath, const SharedPtr<Resource>& InResource, const SharedPtr<MetaBase>& InMeta )
{
    {
        std::lock_guard<std::mutex> lock( m_reimports->Mutex );
        if( std::find( m_reimports->Running.begin(), m_reimports->Running.end(), InPath ) != m_reimports->Running.end() )
        {
            m_reimports->Rerun.insert( InPath );   // export again once this one finishes
            return;
        }
        if( m_reimports->IsPending( InPath ) )
        {
            return;   // already queued
        }
        if( m_reimports->Threads.empty() )
        {
            const int count = static_cast<int>( std::clamp( std::thread::hardware_concurrency() / 4u, 1u, 4u ) );
            for( int i = 0; i < count; ++i )
            {
                m_reimports->Threads.emplace_back( [this, i] { m_reimports->Run( i ); } );
            }
        }
        m_reimports->Queued.push_back( { InPath, InResource, InMeta } );
    }
    m_reimports->WorkReady.notify_one();
}


void ResourceCache::FinishReimport( const std::string& InPath, const SharedPtr<Resource>& InResource, const SharedPtr<MetaBase>& InMeta )
{
#if USING( ME_TOOLS )
    InMeta->Save();
    AssetMetaCache::GetInstance().Update( Path( InPath ), InMeta );
#endif
    InResource->SetMetadata( InMeta );
    CompleteLoad( InResource );
    InResource->Reload();
}


std::vector<std::string> ResourceCache::PumpReimports()
{
    std::vector<std::string> reloaded;
    std::deque<Reimports::Job> done;
    {
        std::lock_guard<std::mutex> lock( m_reimports->Mutex );
        done.swap( m_reimports->Done );
    }
    for( Reimports::Job& job : done )
    {
        bool rerun = false;
        {
            std::lock_guard<std::mutex> lock( m_reimports->Mutex );
            rerun = m_reimports->Rerun.erase( job.Path ) > 0;
        }
        if( rerun )
        {
            // Changed while compiling: the result is stale, compile the latest (and its settings).
            SharedPtr<MetaBase> meta = LoadMetadata( Path( job.Path ) );
            QueueReimport( job.Path, job.Target, meta ? meta : job.Meta );
            continue;
        }
        FinishReimport( job.Path, job.Target, job.Meta );
        reloaded.push_back( job.Path );
    }
    return reloaded;
}


std::vector<std::string> ResourceCache::WaitForReimports()
{
    std::vector<std::string> reloaded;
    while( true )
    {
        {
            std::unique_lock<std::mutex> lock( m_reimports->Mutex );
            m_reimports->WorkDone.wait( lock, [this] { return m_reimports->Queued.empty() && m_reimports->Running.empty(); } );
        }
        std::vector<std::string> batch = PumpReimports();
        reloaded.insert( reloaded.end(), batch.begin(), batch.end() );
        std::lock_guard<std::mutex> lock( m_reimports->Mutex );
        if( m_reimports->Queued.empty() && m_reimports->Running.empty() && m_reimports->Done.empty() )
        {
            return reloaded;
        }
    }
}


std::size_t ResourceCache::GetPendingReimportCount() const
{
    std::lock_guard<std::mutex> lock( m_reimports->Mutex );
    return m_reimports->Queued.size() + m_reimports->Running.size() + m_reimports->Done.size();
}


void ResourceCache::StopReimports()
{
    std::vector<std::thread> threads;
    {
        std::lock_guard<std::mutex> lock( m_reimports->Mutex );
        m_reimports->Stopping = true;
        threads.swap( m_reimports->Threads );
    }
    m_reimports->WorkReady.notify_all();
    for( std::thread& thread : threads )
    {
        thread.join();
    }
    std::lock_guard<std::mutex> lock( m_reimports->Mutex );
    m_reimports->Queued.clear();
    m_reimports->Done.clear();
    m_reimports->Rerun.clear();
    m_reimports->Stopping = false;
}

SharedPtr<MetaBase> ResourceCache::LoadMetadata( const Path& filePath )
{
    SharedPtr<MetaBase> metadata = nullptr;
    MetaRegistry::iterator it = GetMetadatabase().reg.find( filePath.GetExtension() );
    if( it != GetMetadatabase().reg.end() )
    {
        Path metaPath = Path( filePath.FullPath + ".meta", true );
        File metaFile = File( metaPath );
        json j;

        metadata = it->second( filePath );

        if( metaPath.Exists )
        {
            j = json::parse( metaFile.Read() );
            metadata->Deserialize( j );
        }
#if USING( ME_EDITOR )
        else
        {
            metadata->Serialize( j );
            metaFile.Write( j.dump( 4 ) );
            metadata->FlaggedForExport = true;
        }

        metadata->FlaggedForExport = AssetMetaCache::GetInstance().WasModified( filePath, metadata ) || metadata->FlaggedForExport;
#endif
    }
    return metadata;
}
