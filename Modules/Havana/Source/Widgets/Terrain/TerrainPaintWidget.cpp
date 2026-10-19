#include "TerrainWidget.h"
#if USING( ME_EDITOR )
#include "Utils/HavanaUtils.h"
#include "misc/cpp/imgui_stdlib.h"
#include "Events/EditorEvents.h"
#include "Types/AssetDescriptor.h"
#include "Editor/Selection.h"
#include "Components/Graphics/Terrain.h"

bool TerrainWidget::FillLayer( unsigned layer )
{
    if( !m_active || IsPlaying() || layer >= 4 ) return false;
    m_active->EndStroke();
    auto surface = m_active->GetField().GetSurface() ? std::make_shared<TerrainData::Surface>( *m_active->GetField().GetSurface() ) : std::make_shared<TerrainData::Surface>();
    surface->Samples.clear(); surface->BaseWeights = {}; surface->BaseWeights[layer] = 255;
    return m_active->SetSurface( std::move( surface ) );
}


void TerrainWidget::EditSurfaceLayer()
{
    if( !m_active || IsPlaying() || m_active->IsStroking() ) return;
    const auto source = m_active->GetField().GetSurface();
    m_layerDraft = ( source ? *source : TerrainData::Surface() ).Layers[Brush.Layer];
    m_layerIndex = Brush.Layer; m_layerDocument = m_active;
    ++m_layerEditGeneration; m_layerEditRequested = true;
}


void TerrainWidget::DrawLayerMap( unsigned kind, const char* label, std::string& path )
{
    ImGui::PushID( static_cast<int>( kind ) );
    HavanaUtils::Label( label ); ImGui::SetNextItemWidth( 300 );
    ImGui::InputText( "##path", &path );
    const auto a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
    m_layerMapCenters[kind] = Vector2( ( a.x + b.x ) * 0.5f, ( a.y + b.y ) * 0.5f );
    if( ImGui::BeginDragDropTarget() )
    {
        if( ImGui::AcceptDragDropPayload( AssetDescriptor::kDragAndDropPayload ) )
            if( const auto* asset = AssetDescriptor::GetDragged(); asset && asset->Type == AssetType::Texture ) path = asset->FullPath.GetLocalPathString();
        ImGui::EndDragDropTarget();
    }
    ImGui::SameLine();
    if( ImGui::Button( "Browse" ) )
    {
        const auto generation = m_layerEditGeneration;
        RequestAssetSelectionEvent event( [this, kind, generation]( Path selected ) {
            if( generation != m_layerEditGeneration ) return;
            std::string* maps[] = { &m_layerDraft.BaseColorMap, &m_layerDraft.NormalMap, &m_layerDraft.OrmMap };
            *maps[kind] = selected.GetLocalPathString();
        }, AssetType::Texture );
        event.Fire();
    }
    ImGui::SameLine(); if( ImGui::Button( "Clear" ) ) path.clear();
    ImGui::PopID();
}


void TerrainWidget::DrawPaint()
{
    ImGui::SeparatorText( "Surface layers" );
    ImGui::BeginDisabled( !m_active || IsPlaying() || ( m_active && m_active->IsStroking() ) );
    const TerrainData::Surface defaults;
    const auto source = m_active ? m_active->GetField().GetSurface() : nullptr;
    const auto& surface = source ? *source : defaults;
    if( !source )
    {
        ImGui::TextWrapped( "Paint or fill to enable surface layers for this source asset." );
        if( ImGui::Button( "Enable surface layers", ImVec2( -1, 0 ) ) ) m_active->SetSurface( std::make_shared<TerrainData::Surface>() );
    }
    if( ImGui::BeginTable( "Surface palette", 2, ImGuiTableFlags_SizingStretchSame ) )
    {
        for( unsigned i = 0; i < 4; ++i )
        {
            ImGui::TableNextColumn(); ImGui::PushID( static_cast<int>( i ) );
            const auto& layer = surface.Layers[i];
            ImGui::ColorButton( "##swatch", ImVec4( layer.Color[0], layer.Color[1], layer.Color[2], 1 ), ImGuiColorEditFlags_NoTooltip, ImVec2( 22, 22 ) );
            ImGui::SameLine();
            if( ImGui::Selectable( layer.Name.c_str(), Brush.Layer == i, 0, ImVec2( 0, 22 ) ) ) Brush.Layer = i;
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if( ImGui::Button( "Edit selected layer...", ImVec2( -1, 0 ) ) ) EditSurfaceLayer();
    if( source )
    {
        ImGui::TextDisabled( "%zu painted samples", source->Samples.size() );
        if( ImGui::Button( "Disable surface layers", ImVec2( -1, 0 ) ) ) m_active->SetSurface( nullptr );
    }
    ImGui::EndDisabled();
    if( m_layerEditRequested ) { ImGui::OpenPopup( "Edit terrain layer" ); m_layerEditRequested = false; }
    if( ImGui::BeginPopupModal( "Edit terrain layer", nullptr, ImGuiWindowFlags_AlwaysAutoResize ) )
    {
        ImGui::InputText( "Name", &m_layerDraft.Name );
        ImGui::ColorEdit3( "Color", m_layerDraft.Color.data() );
        ImGui::SliderFloat( "Roughness", &m_layerDraft.Roughness, 0, 1 );
        ImGui::SliderFloat( "Metallic", &m_layerDraft.Metallic, 0, 1 );
        ImGui::DragFloat( "Tile size (metres)", &m_layerDraft.TileSize, 0.05f, 0.01f, 4096, "%.2f", ImGuiSliderFlags_AlwaysClamp );
        DrawLayerMap( 0, "Base color", m_layerDraft.BaseColorMap );
        DrawLayerMap( 1, "Normal", m_layerDraft.NormalMap );
        DrawLayerMap( 2, "Packed ORM", m_layerDraft.OrmMap );
        ImGui::TextDisabled( "ORM: R occlusion / G roughness / B metallic" );
        ImGui::SliderFloat( "Normal strength", &m_layerDraft.NormalStrength, 0, 2 );
        ImGui::Checkbox( "Flip normal Y", &m_layerDraft.FlipNormalY );
        ImGui::BeginDisabled( IsPlaying() || m_active != m_layerDocument || m_layerDraft.Name.empty() );
        if( ImGui::Button( "Apply", ImVec2( 120, 0 ) ) )
        {
            auto changed = m_active->GetField().GetSurface() ? std::make_shared<TerrainData::Surface>( *m_active->GetField().GetSurface() ) : std::make_shared<TerrainData::Surface>();
            changed->Layers[m_layerIndex] = m_layerDraft;
            if( m_active->SetSurface( changed ) ) { ++m_layerEditGeneration; ImGui::CloseCurrentPopup(); }
        }
        const auto applyMin = ImGui::GetItemRectMin(), applyMax = ImGui::GetItemRectMax();
        m_layerApplyCenter = Vector2( ( applyMin.x + applyMax.x ) * 0.5f, ( applyMin.y + applyMax.y ) * 0.5f );
        ImGui::EndDisabled(); ImGui::SameLine();
        if( ImGui::Button( "Cancel", ImVec2( 120, 0 ) ) ) { ++m_layerEditGeneration; ImGui::CloseCurrentPopup(); }
        if( m_active && !m_active->GetError().empty() ) ImGui::TextWrapped( "%s", m_active->GetError().c_str() );
        ImGui::EndPopup();
    }
    ImGui::TextWrapped( "Layers blend tiled base color, normal and packed ORM maps. Streaming cooks are planned." );
    if( auto entity = Selection::Get().GetActive() ) if( const auto* terrain = entity->TryGetComponent<Terrain>(); terrain && !terrain->GetLodError().empty() )
        ImGui::TextWrapped( "%s", terrain->GetLodError().c_str() );
    DrawBrush();
}
#endif
