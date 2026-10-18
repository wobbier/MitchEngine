#include "TerrainDocument.h"
#if USING( ME_EDITOR )
#include "Components/Graphics/Terrain.h"
#include "Terrain/TerrainAsset.h"
#include "Resource/AssetDatabase.h"
#include "Resource/ResourceCache.h"
#include "Resource/MetaFile.h"
#include "Utils/GUID.h"
#include "Path.h"
#include "CLog.h"
#include <fstream>

namespace
{
    void RegisterSource( const std::string& path, uint64_t guid )
    {
        if( auto metadata = ResourceCache::GetInstance().LoadMetadata( Path( path ) ) )
        {
            metadata->GUID = guid;
            metadata->Save();
        }
    }
    class SculptCommand : public UndoCommand
    {
    public:
        SculptCommand( TerrainDocument& InDocument, std::vector<TerrainData::SampleDelta> InDeltas )
            : m_document( InDocument ), m_guid( InDocument.GetGUID() ), m_deltas( std::move( InDeltas ) ) {}
        void Undo() override { m_document.ApplyDeltas( m_guid, m_deltas, false ); }
        void Redo() override { m_document.ApplyDeltas( m_guid, m_deltas, true ); }
        std::string GetName() const override { return "Sculpt terrain (" + std::to_string( m_deltas.size() ) + " samples)"; }
    private:
        TerrainDocument& m_document; // Owned history never outlives its document.
        uint64_t m_guid;
        std::vector<TerrainData::SampleDelta> m_deltas;
    };
}

TerrainDocument::TerrainDocument( std::string path ) : m_path( Path( path ).GetLocalPathString() ) {}
TerrainDocument::~TerrainDocument() { if( m_preview ) Terrain::SetEditorPreview( Path( m_path ), nullptr ); }

bool TerrainDocument::Load()
{
    TerrainData::Heightfield field;
    uint64_t guid = 0; std::string text;
    if( !TerrainData::LoadSource( Path( m_path ).FullPath, field, guid, text, m_error ) ) return false;
    // Sidecar identity agrees with runtime assets, including duplicated source manifests.
    try
    {
        std::ifstream meta( Path( m_path ).FullPath + ".meta" );
        if( meta )
        {
            json value; meta >> value;
            const auto id = value.value( "GUID", std::string() );
            if( !id.empty() ) guid = std::stoull( id, nullptr, 16 );
        }
    }
    catch( const std::exception& e ) { m_error = std::string( "Invalid terrain metadata: " ) + e.what(); return false; }
    std::shared_ptr<const TerrainData::ResidentTerrain> preview;
    if( !TerrainData::BuildResidentTerrain( field, guid, preview, m_error ) ) return false;
    m_stroke.Cancel( m_field );
    m_field = std::move( field ); m_guid = guid; m_manifestText = std::move( text );
    m_undo.Clear(); m_historyBytes = 0; m_preview = std::move( preview );
    Terrain::SetEditorPreview( Path( m_path ), m_preview );
    return true;
}

bool TerrainDocument::CreateFlat( const TerrainData::Settings& settings, float height )
{
    if( !settings.Validate( m_error ) ) return false;
    if( static_cast<uint64_t>( settings.TilesX ) * settings.TilesZ * settings.SamplesPerTile() > 1024 * 1024 )
    { m_error = "The resident authoring prototype supports up to one million source samples"; return false; }
    TerrainData::Heightfield field( settings );
    for( uint32_t z = 0; z < settings.TilesZ; ++z ) for( uint32_t x = 0; x < settings.TilesX; ++x )
        if( !field.CreateFlatTile( { settings.MinTile.X + static_cast<int32_t>( x ), settings.MinTile.Z + static_cast<int32_t>( z ) }, height, m_error ) ) return false;
    const uint64_t guid = GUID::Generate(); std::string text;
    std::shared_ptr<const TerrainData::ResidentTerrain> preview;
    if( !TerrainData::BuildResidentTerrain( field, guid, preview, m_error ) ) return false;
    if( !TerrainData::SaveSource( Path( m_path ).FullPath, field, guid, nullptr, text, m_error ) ) return false;
    RegisterSource( m_path, guid );
    m_field = std::move( field ); m_guid = guid; m_manifestText = std::move( text );
    m_undo.Clear(); m_historyBytes = 0; m_preview = std::move( preview );
    Terrain::SetEditorPreview( Path( m_path ), m_preview );
    return true;
}

bool TerrainDocument::Save()
{
    EndStroke();
    std::string text;
    if( !TerrainData::SaveSource( Path( m_path ).FullPath, m_field, m_guid, &m_manifestText, text, m_error ) ) return false;
    m_manifestText = std::move( text ); m_undo.MarkSaved(); m_field.ClearDirty();
    ResourceCache::GetInstance().Evict( Path( m_path ).FullPath );
    return true;
}

bool TerrainDocument::SaveCopy( const std::string& path )
{
    EndStroke(); std::string text;
    const uint64_t guid = GUID::Generate();
    if( !TerrainData::SaveSource( Path( path ).FullPath, m_field, guid, nullptr, text, m_error ) ) return false;
    RegisterSource( path, guid );
    return true;
}

bool TerrainDocument::BeginStroke( const TerrainData::BrushSettings& brush, double x, double z )
{
    return m_stroke.Begin( m_field, brush, x, z, m_error );
}

bool TerrainDocument::MoveStroke( double x, double z ) { return m_stroke.Move( m_field, x, z, m_error ); }

void TerrainDocument::EndStroke( bool cancel )
{
    if( !m_stroke.IsActive() ) return;
    if( cancel ) { m_stroke.Cancel( m_field ); PublishPreview(); return; }
    auto deltas = m_stroke.Finish();
    if( !deltas.empty() )
    {
        const size_t bytes = deltas.size() * sizeof( TerrainData::SampleDelta );
        if( m_historyBytes + bytes > 64 * 1024 * 1024 )
        {
            m_undo.Clear(); m_historyBytes = 0;
            BRUH( "Terrain history reached 64 MiB; older strokes were released" );
        }
        m_historyBytes += bytes;
        m_undo.Push( std::make_unique<SculptCommand>( *this, std::move( deltas ) ) );
    }
    PublishPreview();
}

bool TerrainDocument::PublishPreview()
{
    if( m_preview && m_preview->Heights->GetRevision() == m_field.GetRevision() ) return true;
    std::shared_ptr<const TerrainData::ResidentTerrain> preview;
    if( !TerrainData::BuildResidentTerrain( m_field, m_guid, preview, m_error ) ) return false;
    m_preview = std::move( preview );
    Terrain::SetEditorPreview( Path( m_path ), m_preview );
    return true;
}

void TerrainDocument::ApplyDeltas( uint64_t guid, const std::vector<TerrainData::SampleDelta>& deltas, bool after )
{
    if( guid != m_guid ) return;
    for( const auto& delta : deltas ) m_field.SetSample( delta.Coordinate, after ? delta.After : delta.Before, m_error );
    PublishPreview();
}
#endif
