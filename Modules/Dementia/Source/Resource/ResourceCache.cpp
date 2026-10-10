#include <Resource/ResourceCache.h>

#include <Pointers.h>
#include "MetaRegistry.h"
#include "JSON.h"
#include "File.h"
#include "AssetMetaCache.h"
#include <optional>
#include <chrono>


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


ResourceCache::ResourceCache()
{
    AssetMetaCache::GetInstance().Load();
}

ResourceCache::~ResourceCache()
{
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


void ResourceCache::ReleaseAll()
{
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

#if USING( ME_TOOLS )
        Path sourceFile( sourcePath );
        if( SharedPtr<MetaBase> metaFile = LoadMetadata( sourceFile ) )
        {
            ExportIfNeeded( sourceFile, metaFile, true );
            resource->SetMetadata( metaFile );
        }
#endif
        resource->Reload();
        reloaded.push_back( sourcePath );
    }
    return reloaded;
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
