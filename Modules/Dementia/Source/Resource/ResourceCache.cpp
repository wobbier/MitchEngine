#include <Resource/ResourceCache.h>

#include <Pointers.h>
#include "MetaRegistry.h"
#include "JSON.h"
#include "File.h"
#include "AssetMetaCache.h"
#include <optional>


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
    return m_resourceStack.size();
}

SharedPtr<Resource> ResourceCache::GetCached( const Path& InFilePath )
{
    auto i = m_resourceStack.find( InFilePath.FullPath );
    if( i != m_resourceStack.end() )
    {
        return i->second;
    }
    return {};
}

void ResourceCache::TryToDestroy( Resource* resource )
{
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

ResourceStack& ResourceCache::GetResouceStack() const
{
    return m_resourceStack;
}

void ResourceCache::Dump()
{
    for( ResourceStack::iterator iter = m_resourceStack.begin(); iter != m_resourceStack.end(); )
    {
        if( iter->second.use_count() == 1 )
        {
            iter = m_resourceStack.erase( iter );
        }
        else
        {
            ++iter;
        }
    }
}

SharedPtr<MetaBase> ResourceCache::LoadMetadata( const Path& filePath )
{
    SharedPtr<MetaBase> metadata = nullptr;
    MetaRegistry::iterator it = GetMetadatabase().reg.find( filePath.GetExtension() );
    if( it != GetMetadatabase().reg.end() )
    {
        Path metaPath = Path( filePath.FullPath + ".meta" );
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
