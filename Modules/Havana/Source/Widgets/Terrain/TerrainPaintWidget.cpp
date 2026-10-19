#include "TerrainWidget.h"
#if USING( ME_EDITOR )
#include "Utils/HavanaUtils.h"
#include "misc/cpp/imgui_stdlib.h"

bool TerrainWidget::FillLayer( unsigned layer )
{
    if( !m_active || IsPlaying() || layer >= 4 ) return false;
    m_active->EndStroke();
    auto surface = m_active->GetField().GetSurface() ? std::make_shared<TerrainData::Surface>( *m_active->GetField().GetSurface() ) : std::make_shared<TerrainData::Surface>();
    surface->Samples.clear(); surface->BaseWeights = {}; surface->BaseWeights[layer] = 255;
    return m_active->SetSurface( std::move( surface ) );
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
    if( ImGui::Button( "Edit selected layer...", ImVec2( -1, 0 ) ) )
    {
        m_layerDraft = surface.Layers[Brush.Layer]; m_layerIndex = Brush.Layer; m_layerDocument = m_active;
        ImGui::OpenPopup( "Edit terrain layer" );
    }
    if( source )
    {
        ImGui::TextDisabled( "%zu painted samples", source->Samples.size() );
        if( ImGui::Button( "Disable surface layers", ImVec2( -1, 0 ) ) ) m_active->SetSurface( nullptr );
    }
    ImGui::EndDisabled();
    if( ImGui::BeginPopupModal( "Edit terrain layer", nullptr, ImGuiWindowFlags_AlwaysAutoResize ) )
    {
        ImGui::InputText( "Name", &m_layerDraft.Name );
        ImGui::ColorEdit3( "Color", m_layerDraft.Color.data() );
        ImGui::SliderFloat( "Roughness", &m_layerDraft.Roughness, 0, 1 );
        ImGui::BeginDisabled( IsPlaying() || m_active != m_layerDocument || m_layerDraft.Name.empty() );
        if( ImGui::Button( "Apply", ImVec2( 120, 0 ) ) )
        {
            auto changed = m_active->GetField().GetSurface() ? std::make_shared<TerrainData::Surface>( *m_active->GetField().GetSurface() ) : std::make_shared<TerrainData::Surface>();
            changed->Layers[m_layerIndex] = m_layerDraft;
            if( m_active->SetSurface( changed ) ) ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled(); ImGui::SameLine();
        if( ImGui::Button( "Cancel", ImVec2( 120, 0 ) ) ) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::TextWrapped( "Layers blend color and roughness. Texture maps and streaming cooks are planned." );
    DrawBrush();
}
#endif
