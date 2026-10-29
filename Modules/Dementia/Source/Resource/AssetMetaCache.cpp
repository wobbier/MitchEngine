#include "AssetMetaCache.h"
#include "Utils/PlatformUtils.h"
#include <File.h>
#include <string>
#include "MetaFile.h"
#include <CLog.h>
#include <sys/stat.h>
#include <filesystem>
#include <fstream>
#include <random>

AssetMetaCache::AssetMetaCache()
{
    // Nothing on disk until Save: shipped and cooked runs never write the cache (ME-263).
}

void AssetMetaCache::Init()
{

}

bool AssetMetaCache::WasModified( const Path& filePath, SharedPtr<MetaBase> metaFile )
{
    std::lock_guard<std::mutex> lock( m_mutex );
    bool wasModified = true;
    if( m_cachedAssets.find( filePath.GetLocalPath().data() ) != m_cachedAssets.end() )
    {
        wasModified = m_cachedAssets[filePath.GetLocalPath().data()].LastModified != metaFile->LastModified;
    }
    return wasModified;
}

void AssetMetaCache::Update( const Path& filePath, SharedPtr<MetaBase> metaFile )
{
    if( !metaFile )
    {
        YIKES( "[AssetMetaCache] Trying to update a null MetaBase" );
        return;
    }

    struct stat fileInfo = {};
    if( stat( filePath.FullPath.c_str(), &fileInfo ) != 0 )
    {
        // Use stat() to get the info
        //std::cerr << "Error: " << strerror(errno) << '\n';
    }

    std::lock_guard<std::mutex> lock( m_mutex );

    metaFile->LastModified = static_cast<long>( fileInfo.st_mtime );

    if( m_cachedAssets.find( filePath.GetLocalPath().data() ) != m_cachedAssets.end() )
    {
        m_cachedAssets[filePath.GetLocalPath().data()].LastModified = static_cast<long>( fileInfo.st_mtime );
    }
    else
    {
        CachedAssetInfo info;
        info.LastModified = static_cast<long>( fileInfo.st_mtime );
        m_cachedAssets[filePath.GetLocalPath().data()] = info;
    }

    SaveLocked();
}

void AssetMetaCache::Load()
{
    std::lock_guard<std::mutex> lock( m_mutex );
    if( !IsLoaded )
    {
        Path configPath( kCachePath );
        File cacheFile( configPath );
        if( configPath.Exists )
        {
            cacheFile.Read();
        }
        if( cacheFile.Data.length() > 0 )
        {
            // The cache only says which assets changed since their last export, so a damaged one
            // (e.g. written by another process at the same moment) is rebuilt rather than fatal.
            try
            {
                json cache = json::parse( cacheFile.Data );
                for( const json& i : cache )
                {
                    CachedAssetInfo ent;
                    ent.LastModified = i.at( "LastModified" ).get<long>();
                    m_cachedAssets[i.at( "Key" ).get<std::string>()] = ent;
                }
            }
            catch( const json::exception& e )
            {
                YIKES_FMT( "[AssetMetaCache] Ignoring unreadable %s (%s); it will be rebuilt", kCachePath, e.what() );
                m_cachedAssets.clear();
            }
        }
        IsLoaded = true;
    }
}


void AssetMetaCache::Save()
{
    std::lock_guard<std::mutex> lock( m_mutex );
    SaveLocked();
}


void AssetMetaCache::SaveLocked()
{
    if( m_readOnly )
    {
        return;
    }
    json j_map = json::array();
    for( auto& thing : m_cachedAssets )
    {
        json j;
        j["Key"] = thing.first;
        j["LastModified"] = thing.second.LastModified;
        j_map.push_back( j );
    }

    // Write a private temp file and rename it over the cache, so another process (the editor and a
    // game run share the cache) never reads a half-written file.
    Path configPath( kCachePath );
    const std::filesystem::path target( configPath.FullPath );
    std::filesystem::path temp = target;
    temp += ".tmp" + std::to_string( std::random_device{}() );
    {
        // Packaged runs start in directories without a .tmp/ yet.
        std::error_code directoryError;
        std::filesystem::create_directories( target.parent_path(), directoryError );
        std::ofstream out( temp, std::ios::binary | std::ios::trunc );
        if( !out )
        {
            YIKES_FMT( "[AssetMetaCache] Failed to write %s", temp.string().c_str() );
            return;
        }
        out << j_map.dump( 4 );
    }
    std::error_code ec;
    std::filesystem::rename( temp, target, ec );
    if( ec )
    {
        YIKES_FMT( "[AssetMetaCache] Failed to replace %s (%s)", kCachePath, ec.message().c_str() );
        std::filesystem::remove( temp, ec );
    }
}
