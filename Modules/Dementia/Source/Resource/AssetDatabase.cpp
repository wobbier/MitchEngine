#include "AssetDatabase.h"
#include "JSON.h"
#include <cstdlib>
#include <filesystem>
#include <fstream>


AssetDatabase& AssetDatabase::Get()
{
    static AssetDatabase instance;
    return instance;
}


void AssetDatabase::Refresh( const std::vector<std::string>& InRoots )
{
    namespace fs = std::filesystem;
    for( const std::string& root : InRoots )
    {
        std::error_code ec;
        for( fs::recursive_directory_iterator it( root, fs::directory_options::skip_permission_denied, ec ), end; !ec && it != end; it.increment( ec ) )
        {
            if( !it->is_regular_file( ec ) || it->path().extension() != ".meta" )
            {
                continue;
            }
            std::ifstream stream( it->path() );
            const json meta = json::parse( stream, nullptr, false );
            if( meta.is_discarded() || !meta.contains( "GUID" ) || !meta["GUID"].is_string() )
            {
                continue;
            }
            const uint64_t guid = std::strtoull( meta["GUID"].get<std::string>().c_str(), nullptr, 16 );
            std::string assetPath = it->path().generic_string();
            assetPath.resize( assetPath.size() - 5 );   // strip ".meta"
            Register( assetPath, guid );
        }
    }
}


void AssetDatabase::Register( const std::string& InLocalPath, uint64_t InGUID )
{
    if( InGUID == 0 || InLocalPath.empty() )
    {
        return;
    }
    std::lock_guard<std::mutex> lock( m_mutex );
    auto previous = m_guids.find( InLocalPath );
    if( previous != m_guids.end() && previous->second != InGUID )
    {
        m_paths.erase( previous->second );
    }
    m_guids[InLocalPath] = InGUID;
    m_paths[InGUID] = InLocalPath;
}


void AssetDatabase::Unregister( const std::string& InLocalPath )
{
    std::lock_guard<std::mutex> lock( m_mutex );
    auto found = m_guids.find( InLocalPath );
    if( found != m_guids.end() )
    {
        m_paths.erase( found->second );
        m_guids.erase( found );
    }
}


std::string AssetDatabase::FindPath( uint64_t InGUID ) const
{
    std::lock_guard<std::mutex> lock( m_mutex );
    auto found = m_paths.find( InGUID );
    return found != m_paths.end() ? found->second : std::string();
}


uint64_t AssetDatabase::FindGUID( const std::string& InLocalPath ) const
{
    std::lock_guard<std::mutex> lock( m_mutex );
    auto found = m_guids.find( InLocalPath );
    return found != m_guids.end() ? found->second : 0;
}


size_t AssetDatabase::GetAssetCount() const
{
    std::lock_guard<std::mutex> lock( m_mutex );
    return m_guids.size();
}
