#include "Resource/CookedAssets.h"

#include <cstdlib>
#include <mutex>

#include "CLog.h"
#include "VFS/Mount.h"
#include "VFS/VFS.h"

namespace
{
    // The stripped sources of a cooked tree: they exist (Stat says so) but serve no bytes, so
    // Path( "X.png" ).Exists holds while loaders read the shipped twin ("X.png.dds"). Mounted
    // below every other root, so anything actually shipped under the same path answers first.
    class StrippedSourceMount final
        : public IMountSource
    {
    public:
        explicit StrippedSourceMount( std::set<std::string> InSources )
            : m_sources( std::move( InSources ) )
        {
        }

        VfsStat Stat( const std::string& InRelativePath ) const final
        {
            VfsStat stat;
            stat.Exists = stat.IsFile = m_sources.count( InRelativePath ) > 0;
            return stat;
        }

        std::string GetPhysicalPath( const std::string& ) const final
        {
            return std::string();
        }

        bool ReadText( const std::string&, std::string& ) const final
        {
            return false;
        }

        bool ReadBytes( const std::string&, std::vector<uint8_t>& ) const final
        {
            return false;
        }

        bool WriteText( const std::string&, const std::string& ) const final
        {
            return false;
        }

        void Enumerate( const std::string& InRelativeDirectory, bool InRecursive,
                        const std::function<void( const std::string& InRelativePath, bool InIsDirectory )>& InVisitor ) const final
        {
            const std::string prefix = InRelativeDirectory.empty() ? std::string() : InRelativeDirectory + "/";
            for( auto it = m_sources.lower_bound( prefix ); it != m_sources.end() && it->compare( 0, prefix.size(), prefix ) == 0; ++it )
            {
                if( InRecursive || it->find( '/', prefix.size() ) == std::string::npos )
                {
                    InVisitor( *it, false );
                }
            }
        }

        bool GetStreamWindow( const std::string&, VfsStreamWindow& ) const final
        {
            return false;
        }

    private:
        std::set<std::string> m_sources;
    };

    std::mutex s_mountMutex;
    SharedPtr<IMountSource> s_strippedMount;
}


CookedAssets& CookedAssets::Get()
{
    static CookedAssets instance;
    return instance;
}


bool CookedAssets::Load()
{
    std::string text;
    if( !VFS::Get().ReadText( std::string( kRegistryPath ), text ) )
    {
        return false;
    }
    const json registry = json::parse( text, nullptr, false );
    if( !LoadFrom( registry ) )
    {
        CLog::Log( CLog::LogType::Error, std::string( "Unreadable cooked asset registry " ) + kRegistryPath );
        return false;
    }
    CLog::Log( CLog::LogType::Info, "Cooked build (" + m_platform + "): " + std::to_string( m_metas.size() ) + " import settings, " + std::to_string( m_guids.size() ) + " GUIDs" );
    return true;
}


bool CookedAssets::LoadFrom( const json& InRegistry )
{
    Reset();
    if( !InRegistry.is_object() || !InRegistry.contains( "Assets" ) || !InRegistry["Assets"].is_object() )
    {
        return false;
    }
    m_platform = InRegistry.value( "Platform", std::string() );
    if( auto atlases = InRegistry.find( "SpriteAtlases" ); atlases != InRegistry.end() && atlases->is_object() )
    {
        for( auto it = atlases->begin(); it != atlases->end(); ++it )
        {
            if( it.value().is_object() )
            {
                m_atlasSprites[it.key()] = AtlasSprite{ it.value().value( "Sheet", std::string() ), it.value().value( "Sprite", std::string() ) };
            }
        }
    }
    std::set<std::string> stripped;
    for( auto it = InRegistry["Assets"].begin(); it != InRegistry["Assets"].end(); ++it )
    {
        const json& entry = it.value();
        if( !entry.is_object() )
        {
            continue;
        }
        if( entry.value( "Stripped", false ) )
        {
            stripped.insert( it.key() );
        }
        if( entry.contains( "Meta" ) && entry["Meta"].is_object() )
        {
            m_metas[it.key()] = entry["Meta"];
        }
        if( entry.contains( "GUID" ) && entry["GUID"].is_string() )
        {
            const uint64_t guid = std::strtoull( entry["GUID"].get<std::string>().c_str(), nullptr, 16 );
            if( guid != 0 )
            {
                m_guids[it.key()] = guid;
            }
        }
        if( entry.contains( "Textures" ) && entry["Textures"].is_object() )
        {
            auto& references = m_textureReferences[it.key()];
            for( auto texture = entry["Textures"].begin(); texture != entry["Textures"].end(); ++texture )
            {
                if( texture.value().is_string() )
                {
                    references[texture.key()] = texture.value().get<std::string>();
                }
            }
        }
    }
    if( InRegistry.contains( "EngineShaderStages" ) && InRegistry["EngineShaderStages"].is_array() )
    {
        for( const json& stage : InRegistry["EngineShaderStages"] )
        {
            if( stage.is_string() )
            {
                m_engineShaderStages.push_back( stage.get<std::string>() );
            }
        }
    }
    {
        std::lock_guard<std::mutex> lock( s_mountMutex );
        s_strippedMount = MakeShared<StrippedSourceMount>( std::move( stripped ) );
        VFS::Get().Mount( "", s_strippedMount, -1000, false );
    }
    m_cooked.store( true, std::memory_order_release );
    return true;
}


void CookedAssets::Reset()
{
    m_cooked.store( false, std::memory_order_release );
    {
        std::lock_guard<std::mutex> lock( s_mountMutex );
        if( s_strippedMount )
        {
            VFS::Get().Unmount( s_strippedMount );
            s_strippedMount.reset();
        }
    }
    m_platform.clear();
    m_metas.clear();
    m_textureReferences.clear();
    m_guids.clear();
    m_atlasSprites.clear();
    m_engineShaderStages.clear();
}


const json* CookedAssets::FindMeta( const std::string& InLogicalPath ) const
{
    if( !IsCooked() )
    {
        return nullptr;
    }
    auto it = m_metas.find( InLogicalPath );
    return it == m_metas.end() ? nullptr : &it->second;
}


std::string CookedAssets::FindTextureReference( const std::string& InModel, const std::string& InReference ) const
{
    if( !IsCooked() )
    {
        return std::string();
    }
    auto model = m_textureReferences.find( InModel );
    if( model == m_textureReferences.end() )
    {
        return std::string();
    }
    auto texture = model->second.find( InReference );
    return texture == model->second.end() ? std::string() : texture->second;
}


const CookedAssets::AtlasSprite* CookedAssets::FindAtlasSprite( const std::string& InKey ) const
{
    if( !IsCooked() )
    {
        return nullptr;
    }
    auto it = m_atlasSprites.find( InKey );
    return it != m_atlasSprites.end() ? &it->second : nullptr;
}
