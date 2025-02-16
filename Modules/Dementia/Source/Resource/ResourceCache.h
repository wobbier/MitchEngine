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

class Resource;

typedef const std::map<std::string, std::shared_ptr<Resource>> ResourceStack;

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

    ResourceStack& GetResouceStack() const;

    void Dump();

    SharedPtr<MetaBase> LoadMetadata( const Path& filePath );

private:
    std::map<std::string, std::shared_ptr<Resource>> m_resourceStack;

    ME_SINGLETON_DEFINITION( ResourceCache )
};

template<class T, typename... Args>
SharedPtr<T> ResourceCache::Get( const Path& InFilePath, Args&& ... args )
{
    auto I = m_resourceStack.find( InFilePath.FullPath );
    if( I != m_resourceStack.end() )
    {
        SharedPtr<T> Res = std::dynamic_pointer_cast<T>( I->second );
        return Res;
    }

    SharedPtr<MetaBase> metaFile = LoadMetadata( InFilePath );

    bool compiledFileExists = true;
    if( metaFile )
    {
        Path compiledAsset = Path( metaFile->FilePath.FullPath + "." + metaFile->GetExtension2() );
        compiledFileExists = compiledAsset.Exists;
    }

#if USING( ME_TOOLS )
    if( metaFile && ( metaFile->FlaggedForExport || !compiledFileExists ) )
    {
        YIKES( "Exporting asset: " + InFilePath.FullPath );
        metaFile->Export();
        metaFile->Save();
        AssetMetaCache::GetInstance().Update( InFilePath, metaFile );
    }
#endif

    if( !InFilePath.Exists && !compiledFileExists && metaFile && !metaFile->FlaggedForExport )
    {
        YIKES( "Failed to load resource: " + InFilePath.FullPath );
        return {};
    }

    if( !InFilePath.Exists )
    {
        YIKES( "Shit don't exist bro: " + InFilePath.FullPath );
    }

    SharedPtr<T> Res = MakeShared<T>( InFilePath, std::forward<Args>( args )... );
    Res->Resources = this;
    TypeId id = ClassTypeId<Resource>::GetTypeId<T>();
    Res->ResourceType = static_cast<std::size_t>( id );
    Res->SetMetadata( metaFile );
    Res->Load();
    m_resourceStack[InFilePath.FullPath] = Res;
    return Res;
}
