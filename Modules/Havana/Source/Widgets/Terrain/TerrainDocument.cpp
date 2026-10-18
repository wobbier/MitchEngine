#include "TerrainDocument.h"
#if USING( ME_EDITOR )
#include "Components/Graphics/Terrain.h"
#include "Terrain/TerrainAsset.h"
#include "Resource/ResourceCache.h"
#include "Resource/MetaFile.h"
#include "Utils/GUID.h"
#include "Path.h"
#include "CLog.h"
#include <fstream>
#include <ctime>

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
            if( !id.empty() ) { const uint64_t metadataGUID = std::stoull( id, nullptr, 16 ); if( metadataGUID ) guid = metadataGUID; }
        }
    }
    catch( const std::exception& e ) { m_error = std::string( "Invalid terrain metadata: " ) + e.what(); return false; }
    std::shared_ptr<const TerrainData::ResidentTerrain> preview;
    if( !TerrainData::BuildResidentTerrain( field, guid, preview, m_error ) ) return false;
    m_stroke.Cancel( m_field );
    m_field = std::move( field ); m_guid = guid; m_manifestText = std::move( text );
    m_undo.Clear(); m_historyBytes = 0; m_preview = std::move( preview );
    Terrain::SetEditorPreview( Path( m_path ), m_preview );
    ClearRecovery();
    return true;
}

bool TerrainDocument::CreateFlat( const TerrainData::Settings& settings, float height )
{
    if( !settings.Validate( m_error ) ) return false;
    if( static_cast<uint64_t>( settings.TilesX ) * settings.TilesZ > 1024 * 1024 / settings.SamplesPerTile() )
    { m_error = "The resident authoring prototype supports up to one million source samples"; return false; }
    TerrainData::Heightfield field( settings );
    for( uint32_t z = 0; z < settings.TilesZ; ++z ) for( uint32_t x = 0; x < settings.TilesX; ++x )
        if( !field.CreateFlatTile( { settings.MinTile.X + static_cast<int32_t>( x ), settings.MinTile.Z + static_cast<int32_t>( z ) }, height, m_error ) ) return false;
    return CreateFromField( std::move( field ) );
}

bool TerrainDocument::CreateFromField( TerrainData::Heightfield field )
{
    const auto& settings = field.GetSettings();
    if( static_cast<uint64_t>( settings.TilesX ) * settings.TilesZ > 1024 * 1024 / settings.SamplesPerTile() )
    { m_error = "The resident authoring prototype supports up to one million source samples"; return false; }
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
    if( !CheckSourceIdentity() ) return false;
    std::string text;
    if( !TerrainData::SaveSource( Path( m_path ).FullPath, m_field, m_guid, &m_manifestText, text, m_error ) ) return false;
    m_manifestText = std::move( text ); m_undo.MarkSaved(); m_field.ClearDirty();
    ResourceCache::GetInstance().Evict( Path( m_path ).FullPath );
    ClearRecovery();
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

bool TerrainDocument::CheckSourceIdentity()
{
    try
    {
        std::ifstream meta( Path( m_path ).FullPath + ".meta" );
        if( !meta ) return true;
        json value; meta >> value;
        const auto id = value.value( "GUID", std::string() );
        const uint64_t guid = id.empty() ? 0 : std::stoull( id, nullptr, 16 );
        if( guid && guid != m_guid ) { m_error = "Terrain asset identity changed on disk. Save a copy or revert before saving"; return false; }
        return true;
    }
    catch( const std::exception& e ) { m_error = std::string( "Could not verify terrain metadata: " ) + e.what(); return false; }
}

bool TerrainDocument::OwnsRecovery( const std::filesystem::path& path ) const
{
    const auto key = path.lexically_normal();
    if( !m_recoveryPath.empty() && key == m_recoveryPath.lexically_normal() ) return true;
    for( const auto& restored : m_restoredRecoveries ) if( key == restored.lexically_normal() ) return true;
    return false;
}

bool TerrainDocument::SaveRecovery( const std::filesystem::path& path )
{
    if( IsStroking() ) { m_error = "Finish the terrain stroke before taking a recovery snapshot"; return false; }
    if( !IsDirty() ) { ClearRecovery(); return true; }
    std::error_code existsError;
    if( m_hasRecoveryRevision && path == m_recoveryPath && m_recoveryRevision == m_field.GetRevision() && std::filesystem::exists( path, existsError ) ) return true;
    TerrainData::RecoveryInfo info;
    info.AssetPath = m_path; info.AssetGUID = m_guid; info.SavedAt = static_cast<uint64_t>( std::time( nullptr ) ); info.BaseManifest = m_manifestText;
    if( !TerrainData::WriteRecovery( path, info, m_field, m_error ) ) return false;
    if( !m_recoveryPath.empty() && m_recoveryPath != path ) m_restoredRecoveries.push_back( m_recoveryPath );
    m_recoveryPath = path; m_recoveryRevision = m_field.GetRevision(); m_hasRecoveryRevision = true;
    return true;
}

bool TerrainDocument::RestoreRecovery( const std::filesystem::path& path )
{
    if( IsDirty() ) { m_error = "Save, save a copy, or revert the open terrain before restoring a recovery snapshot"; return false; }
    TerrainData::Heightfield field; TerrainData::RecoveryInfo info;
    if( !TerrainData::ReadRecovery( path, info, field, m_error ) ) return false;
    if( std::filesystem::path( Path( info.AssetPath ).FullPath ).lexically_normal() != std::filesystem::path( Path( m_path ).FullPath ).lexically_normal() )
    { m_error = "Recovery snapshot belongs to another terrain asset"; return false; }
    std::shared_ptr<const TerrainData::ResidentTerrain> preview;
    if( !TerrainData::BuildResidentTerrain( field, info.AssetGUID, preview, m_error ) ) return false;
    m_field = std::move( field ); m_guid = info.AssetGUID; m_manifestText = std::move( info.BaseManifest );
    m_undo.Clear(); m_undo.MarkDirty(); m_historyBytes = 0; m_preview = std::move( preview );
    m_restoredRecoveries.push_back( path ); m_hasRecoveryRevision = false;
    Terrain::SetEditorPreview( Path( m_path ), m_preview );
    return true;
}

void TerrainDocument::ClearRecovery()
{
    if( !m_recoveryPath.empty() ) m_restoredRecoveries.push_back( m_recoveryPath );
    m_recoveryPath.clear(); m_hasRecoveryRevision = false;
    std::vector<std::filesystem::path> remaining;
    for( const auto& path : m_restoredRecoveries )
    {
        std::error_code error;
        std::filesystem::remove( path, error );
        if( error ) { remaining.push_back( path ); BRUH( "Could not remove terrain recovery: " + error.message() ); }
    }
    m_restoredRecoveries = std::move( remaining );
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
            DiscardHistory();
            BRUH( "Terrain history reached 64 MiB; older strokes were released" );
        }
        m_historyBytes += bytes;
        m_undo.Push( std::make_unique<SculptCommand>( *this, std::move( deltas ) ) );
    }
    PublishPreview();
}

void TerrainDocument::DiscardHistory()
{
    const bool dirty = m_undo.IsDirty();
    m_undo.Clear(); m_historyBytes = 0;
    if( dirty ) m_undo.MarkDirty();
}

bool TerrainDocument::PublishPreview()
{
    if( m_preview && m_preview->Heights->GetRevision() == m_field.GetRevision() ) return true;
    std::shared_ptr<const TerrainData::ResidentTerrain> preview;
    if( !TerrainData::UpdateResidentTerrain( m_field, m_guid, m_preview, preview, m_error ) ) return false;
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
