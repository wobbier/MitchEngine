#include "PCH.h"
#include "TerrainResource.h"
#include "Terrain/TerrainSerialization.h"
#include "Terrain/TerrainAsset.h"
#include "Terrain/TerrainStreamArchive.h"
#include "Resource/MetaRegistry.h"
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace
{
    std::vector<uint8_t> ReadBytes( const std::filesystem::path& InPath )
    {
        std::ifstream file( InPath, std::ios::binary | std::ios::ate );
        if( !file ) throw std::runtime_error( "Could not open terrain file: " + InPath.string() );
        const auto size = file.tellg();
        if( size < 0 || static_cast<uint64_t>( size ) > std::numeric_limits<size_t>::max() ) throw std::runtime_error( "Invalid terrain file size" );
        std::vector<uint8_t> bytes( static_cast<size_t>( size ) );
        file.seekg( 0 );
        if( !bytes.empty() && !file.read( reinterpret_cast<char*>( bytes.data() ), size ) ) throw std::runtime_error( "Could not read terrain file: " + InPath.string() );
        return bytes;
    }
}

// Source and cooked files are already explicit terrain formats, so metadata is pass-through.
// Terrain's editor document will own source publication and cooking rather than texture import.
struct TerrainMetadata : MetaBase
{
    explicit TerrainMetadata( const Path& InPath ) : MetaBase( InPath ) { FileType = "Terrain"; }
    std::string GetExtension2() const override { return std::string( FilePath.GetExtension() ); }
    void OnSerialize( json& ) override {}
    void OnDeserialize( const json& ) override {}
};
ME_REGISTER_METADATA( "terrain", TerrainMetadata );
struct TerrainCookedMetadata : TerrainMetadata
{
    using TerrainMetadata::TerrainMetadata;
};
ME_REGISTER_METADATA( "terrainc", TerrainCookedMetadata );

bool TerrainResource::LoadAsync()
{
    m_pending.reset(); m_pendingArchive.reset(); m_error.clear();
    try
    {
        TerrainData::Heightfield field;
        uint64_t guid = 0;
        if( std::string_view( FilePath.GetExtension() ) == "terrainc" )
        {
            std::ifstream file( FilePath.FullPath, std::ios::binary );
            std::array<uint8_t, 4> prefix{}; file.read( reinterpret_cast<char*>( prefix.data() ), prefix.size() );
            if( TerrainData::IsStreamArchive( prefix ) )
            {
                if( !TerrainData::OpenStreamArchive( FilePath.FullPath, m_pendingArchive, m_error ) ) return false;
                m_pending = m_pendingArchive->Coverage;
                if( Metadata && Metadata->GUID && Metadata->GUID != m_pending->AssetGUID )
                {
                    auto data = std::make_shared<TerrainData::ResidentTerrain>( *m_pending ); data->AssetGUID = Metadata->GUID;
                    auto archive = std::make_shared<TerrainData::StreamArchive>( *m_pendingArchive ); archive->AssetGUID = Metadata->GUID; archive->Coverage = data;
                    m_pendingArchive = archive; m_pending = data;
                }
                return true;
            }
        }
        if( std::string_view( FilePath.GetExtension() ) == "terrainc" )
        {
            const auto bytes = ReadBytes( FilePath.FullPath );
            TerrainData::CookInfo info;
            if( !TerrainData::ReadCookedHeightfield( bytes, field, info, m_error ) ) return false;
            guid = info.AssetGUID;
        }
        else
        {
            std::string manifestText;
            if( !TerrainData::LoadSource( FilePath.FullPath, field, guid, manifestText, m_error ) ) return false;
        }
        // The asset database's sidecar identity wins for moved/duplicated engine assets.
        // Embedded GUIDs remain usable when loading a standalone source/cooked file directly.
        if( Metadata && Metadata->GUID ) guid = Metadata->GUID;
        return TerrainData::BuildResidentTerrain( field, guid, m_pending, m_error );
    }
    catch( const std::exception& e ) { m_error = e.what(); return false; }
}

bool TerrainResource::FinishAsyncLoad()
{
    if( !m_pending ) return false;
    m_data = std::move( m_pending );
    m_archive = std::move( m_pendingArchive );
    return true;
}
