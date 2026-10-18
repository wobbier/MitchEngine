#include "PCH.h"
#include "TerrainResource.h"
#include "Terrain/TerrainSerialization.h"
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
    m_pending.reset(); m_error.clear();
    try
    {
        TerrainData::Heightfield field;
        uint64_t guid = 0;
        const auto bytes = ReadBytes( FilePath.FullPath );
        if( bytes.empty() ) { m_error = "Terrain file is empty"; return false; }
        if( std::string_view( FilePath.GetExtension() ) == "terrainc" )
        {
            TerrainData::CookInfo info;
            if( !TerrainData::ReadCookedHeightfield( bytes, field, info, m_error ) ) return false;
            guid = info.AssetGUID;
        }
        else
        {
            TerrainData::Manifest manifest;
            if( !TerrainData::DeserializeManifest( std::string_view( reinterpret_cast<const char*>( bytes.data() ), bytes.size() ), manifest, m_error ) ) return false;
            guid = manifest.AssetGUID;
            field = TerrainData::Heightfield( manifest.Config );
            const auto directory = std::filesystem::path( FilePath.FullPath ).parent_path();
            for( const auto& reference : manifest.Pages )
            {
                TerrainData::TilePage page;
                const auto source = ReadBytes( directory / reference.Source );
                if( !TerrainData::DeserializePage( source, manifest.Config, page, m_error ) ) return false;
                if( page.Coordinate != reference.Coordinate ) { m_error = "Terrain source page coordinates disagree with its manifest reference"; return false; }
                if( !field.LoadTile( page.Coordinate, std::move( page.Heights ), m_error ) ) return false;
            }
            field.ClearDirty();
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
    return true;
}
