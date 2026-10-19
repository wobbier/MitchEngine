#include "PCH.h"
#include "Terrain.h"
#include "Resource/ResourceCache.h"
#include "Resource/AssetDatabase.h"
#include "Resources/TerrainResource.h"
#include "Terrain/TerrainStreamCache.h"
#if USING( ME_EDITOR )
#include "Events/EditorEvents.h"
#include <filesystem>
#include <map>
namespace
{
    std::map<std::string, std::shared_ptr<const TerrainData::ResidentTerrain>> s_editorTerrainPreviews;
    std::string PreviewKey( const Path& path ) { return std::filesystem::path( path.FullPath ).lexically_normal().generic_string(); }
}
void Terrain::SetEditorPreview( const Path& asset, std::shared_ptr<const TerrainData::ResidentTerrain> data )
{
    if( data ) s_editorTerrainPreviews[PreviewKey( asset )] = std::move( data );
    else s_editorTerrainPreviews.erase( PreviewKey( asset ) );
}
#endif

ME_REFLECT_BEGIN( Terrain )
    ME_FIELD( Asset ).Asset( "Terrain" ).Tooltip( "A terrain manifest (.terrain) or cooked terrain (.terrainc)" );
    ME_FIELD( Color ).Color();
    ME_FIELD( Roughness ).Range( 0.f, 1.f ).Speed( 0.01f );
    ME_FIELD( CastShadows );
    ME_FIELD( LodPixelError ).Category( "LOD" ).Range( 0.25f, 32.f );
    ME_FIELD( LodHysteresis ).Category( "LOD" ).Range( 0.f, 0.9f );
    ME_FIELD( ForcedLod ).Category( "LOD" ).NoSerialize().Range( -1, 10 ).Tooltip( "-1 selects by pixel error; 0 is full detail" );
    ME_FIELD( ShowLods ).Category( "LOD" ).NoSerialize();
    ME_FIELD( ShowPatches ).Category( "LOD" ).NoSerialize();
    ME_FIELD( StreamDetail ).Category( "Streaming" ).Tooltip( "Load nearby detail from streamed cooked terrain; disabling this retains the coarse visual horizon" );
    ME_FIELD( StreamDistance ).Category( "Streaming" ).Range( 0.f, 16384.f );
    ME_FIELD( CoverageDistance ).Category( "Streaming" ).Range( 0.f, 16384.f ).Tooltip( "Paged visual horizon; coarse pages outside this radius retire. Detail distance is clipped to this horizon." );
    ME_FIELD( CollisionDistance ).Category( "Streaming" ).Range( 1.f, 1024.f ).Tooltip( "Safety margin around characters and moving bodies, independent of cameras" );
    ME_FIELD( CpuBudgetMiB ).Category( "Streaming" ).Range( 1, 4096 );
    ME_FIELD( GpuBudgetMiB ).Category( "Streaming" ).Range( 1, 4096 );
    ME_FIELD( CollisionBudgetMiB ).Category( "Streaming" ).Range( 1, 4096 );
    ME_FIELD( UploadKiB ).Category( "Streaming" ).Range( 16, 16384 ).Tooltip( "Per-frame terrain upload allowance, including patch geometry and painted weights" );
    ME_FIELD( UploadMilliseconds ).Category( "Streaming" ).Range( 0.1f, 16.f );
    ME_FIELD( ShowResidency ).Category( "Streaming" ).NoSerialize();
    ME_FIELD( Collision ).Category( "Collision" );
    ME_FIELD( Friction ).Category( "Collision" ).Range( 0.f, 2.f );
    ME_FIELD( Restitution ).Category( "Collision" ).Range( 0.f, 1.f );
ME_REFLECT_END()

void Terrain::SetData( std::shared_ptr<const TerrainData::ResidentTerrain> InData )
{
    Asset = Path(); m_loadedAsset.clear(); m_resource.reset(); m_stream.reset();
    m_patchLods.clear(); m_lodTriangles = m_fullTriangles = 0; m_lodError.clear();
    m_data = std::move( InData ); m_valid = false; m_error.clear();
}

bool Terrain::IsLoading() const { return m_resource && m_resource->IsLoading(); }

void Terrain::ReloadAsset()
{
    if( Asset.GetLocalPathString().empty() ) return;
    if( m_resource ) ResourceCache::GetInstance().Evict( m_resource->GetPath().FullPath );
    m_resource.reset(); m_stream.reset(); m_data.reset(); m_loadedAsset.clear(); m_valid = false;
    m_patchLods.clear(); m_lodTriangles = m_fullTriangles = 0; m_lodError.clear();
}

void Terrain::Resolve( bool InWait )
{
#if USING( ME_EDITOR )
    // Scene undo can restore a former path after an asset move. Keep its shared preview usable.
    const auto moved = AssetDatabase::Get().FindMovedPath( Asset.GetLocalPathString() );
    if( !moved.empty() && !std::filesystem::exists( Asset.FullPath ) ) Asset = Path( moved );
#endif
    const std::string path = Asset.GetLocalPathString();
#if USING( ME_EDITOR )
    if( !path.empty() )
    {
        auto preview = s_editorTerrainPreviews.find( PreviewKey( Asset ) );
        if( preview != s_editorTerrainPreviews.end() )
        {
            m_stream.reset(); m_data = preview->second; m_valid = true; m_error.clear();
            return;
        }
    }
#endif
    if( path != m_loadedAsset )
    {
        m_loadedAsset = path; m_resource.reset(); m_stream.reset(); m_data.reset();
        const Path current( path );
        if( !path.empty() && ( current.Exists || !AssetDatabase::Get().FindMovedPath( path ).empty() ) )
            m_resource = ResourceCache::GetInstance().GetAsync<TerrainResource>( current );
    }
    if( m_resource )
    {
        if( InWait && m_resource->IsLoading() ) ResourceCache::GetInstance().CompleteLoad( m_resource );
        if( m_resource->IsLoading() ) { m_error = "Loading terrain"; m_valid = false; return; }
        if( m_resource->HasLoadFailed() ) { m_error = m_resource->GetError(); m_valid = false; return; }
        m_data = m_stream ? m_stream->GetData() : m_resource->GetData();
    }
    m_valid = m_data != nullptr;
    m_error = m_valid ? "" : ( path.empty() ? "Select a terrain asset" : "Terrain asset could not be loaded" );
}


const TerrainData::StreamStats* Terrain::GetStreamingStats() const { return m_stream ? &m_stream->GetStats() : nullptr; }


void Terrain::WaitForStreamingRequest() { if( m_stream ) m_stream->WaitForRequest(); }

#if USING( ME_EDITOR )
void Terrain::OnEditorInspect()
{
    if( std::string_view( Asset.GetExtension() ) == "terrain" && ImGui::Button( "Edit Terrain" ) )
    {
        OpenTerrainEvent event; event.AssetPath = Asset.GetLocalPathString(); event.Fire();
    }
    if( !Asset.GetLocalPathString().empty() && !IsLoading() && ImGui::Button( "Reload Terrain" ) ) ReloadAsset();
    if( !m_error.empty() ) ImGui::TextWrapped( "%s", m_error.c_str() );
    if( auto data = GetData() )
        ImGui::Text( "%zu tiles, %zu render patches", data->CollisionTiles.size(), data->Patches.size() );
}
#endif
