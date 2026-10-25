#include "AssetDatabase.h"
#include "CookedAssets.h"
#include "JSON.h"
#include "CLog.h"
#include "Path.h"
#include <cstdlib>
#include <filesystem>
#include <fstream>


AssetDatabase& AssetDatabase::Get()
{
    static AssetDatabase instance;
    return instance;
}


namespace
{
    // Prefabs carry their GUID inside; the key sorts first, so it's near the top of the file.
    uint64_t ReadPrefabGUID( const std::filesystem::path& InPath )
    {
        std::ifstream stream( InPath, std::ios::binary );
        std::string head( 4096, '\0' );
        stream.read( head.data(), static_cast<std::streamsize>( head.size() ) );
        head.resize( static_cast<size_t>( stream.gcount() ) );
        const size_t key = head.find( "\"AssetGUID\"" );
        if( key == std::string::npos )
        {
            return 0;
        }
        const size_t open = head.find( '"', key + 11 );
        const size_t close = open == std::string::npos ? std::string::npos : head.find( '"', open + 1 );
        if( close == std::string::npos )
        {
            return 0;
        }
        return std::strtoull( head.substr( open + 1, close - open - 1 ).c_str(), nullptr, 16 );
    }
}


void AssetDatabase::Refresh( const std::vector<std::string>& InRoots )
{
    namespace fs = std::filesystem;
    // Two files with one GUID (copied outside the editor) make references ambiguous.
    std::unordered_map<uint64_t, std::string> seen;
    auto registerScanned = [this, &seen]( const std::string& InPath, uint64_t InGUID ) {
        if( InGUID == 0 )
        {
            return;
        }
        auto [previous, inserted] = seen.try_emplace( InGUID, InPath );
        if( !inserted )
        {
            CLog::Log( CLog::LogType::Warning, "Assets '" + previous->second + "' and '" + InPath + "' share a GUID (copied outside the editor?); references to it may resolve to either. Delete one's .meta (or the prefab's \"AssetGUID\") to give it a new one." );
        }
        Register( InPath, InGUID );
    };
    for( const std::string& root : InRoots )
    {
        // Scan the folder the root resolves to (an engine outside the project for "Engine/Assets"),
        // registering paths in the root's own spelling.
        const fs::path physicalRoot = fs::path( Path( root, true ).FullPath );
        auto logicalPath = [&root, &physicalRoot]( const fs::path& InPhysical ) {
            std::error_code relativeError;
            const fs::path relative = fs::relative( InPhysical, physicalRoot, relativeError );
            return relativeError ? InPhysical.generic_string() : ( fs::path( root ) / relative ).generic_string();
        };
        std::error_code ec;
        for( fs::recursive_directory_iterator it( physicalRoot, fs::directory_options::skip_permission_denied, ec ), end; !ec && it != end; it.increment( ec ) )
        {
            if( !it->is_regular_file( ec ) )
            {
                continue;
            }
            if( it->path().extension() == ".prefab" )
            {
                registerScanned( logicalPath( it->path() ), ReadPrefabGUID( it->path() ) );
                continue;
            }
            if( it->path().extension() != ".meta" )
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
            std::string assetPath = logicalPath( it->path() );
            assetPath.resize( assetPath.size() - 5 );   // strip ".meta"
            registerScanned( assetPath, guid );
        }
    }
    std::lock_guard<std::mutex> lock( m_mutex );
    m_scanned = true;
}


void AssetDatabase::EnsureScanned()
{
    if( IsScanned() )
    {
        return;
    }
    // A cooked tree has no .meta files to scan: its registry carries the GUIDs.
    if( CookedAssets::Get().IsCooked() )
    {
        for( const auto& [path, guid] : CookedAssets::Get().GetGuids() )
        {
            Register( path, guid );
        }
        std::lock_guard<std::mutex> lock( m_mutex );
        m_scanned = true;
        return;
    }
    Refresh( { "Assets", "Engine/Assets" } );
}


bool AssetDatabase::IsScanned() const
{
    std::lock_guard<std::mutex> lock( m_mutex );
    return m_scanned;
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
    const std::string folder = InLocalPath + "/";
    for( auto it = m_guids.begin(); it != m_guids.end(); )
    {
        if( it->first == InLocalPath || it->first.rfind( folder, 0 ) == 0 )
        {
            m_paths.erase( it->second );
            it = m_guids.erase( it );
        }
        else
        {
            ++it;
        }
    }
}


void AssetDatabase::Move( const std::string& InFrom, const std::string& InTo )
{
    std::lock_guard<std::mutex> lock( m_mutex );
    const std::string folder = InFrom + "/";
    std::vector<std::pair<std::string, uint64_t>> moved;
    for( const auto& [path, guid] : m_guids )
    {
        if( path == InFrom || path.rfind( folder, 0 ) == 0 )
        {
            moved.emplace_back( path, guid );
        }
    }
    for( const auto& [path, guid] : moved )
    {
        const std::string newPath = InTo + path.substr( InFrom.size() );
        m_guids.erase( path );
        m_guids[newPath] = guid;
        m_paths[guid] = newPath;
        m_formerPaths[path] = guid;
        m_formerPaths.erase( newPath );   // moved back
    }
}


std::string AssetDatabase::FindMovedPath( const std::string& InFormerPath ) const
{
    std::lock_guard<std::mutex> lock( m_mutex );
    auto former = m_formerPaths.find( InFormerPath );
    if( former == m_formerPaths.end() )
    {
        return {};
    }
    auto current = m_paths.find( former->second );
    return current != m_paths.end() ? current->second : std::string();
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
    if( found != m_guids.end() )
    {
        return found->second;
    }
    auto former = m_formerPaths.find( InLocalPath );
    return former != m_formerPaths.end() ? former->second : 0;
}


size_t AssetDatabase::GetAssetCount() const
{
    std::lock_guard<std::mutex> lock( m_mutex );
    return m_guids.size();
}
