#include "TerrainWidget.h"
#if USING( ME_EDITOR )
#include "Editor/Selection.h"
#include "Components/Graphics/Terrain.h"
#include "ImGuiTheme.h"
#include <array>
#include <algorithm>

namespace
{
    Terrain* SelectedTerrain()
    {
        const auto entity = Selection::Get().GetActive();
        return entity ? entity->TryGetComponent<Terrain>() : nullptr;
    }
}


void TerrainWidget::DrawLodButtons()
{
    auto* terrain = SelectedTerrain();
    ImGui::TableNextColumn();
    if( Button( "layout-grid", "Patches", "Outline the selected terrain's render patches in World View.", terrain != nullptr, terrain && terrain->ShowPatches ) ) terrain->ShowPatches = !terrain->ShowPatches;
    m_lodButtonCenters[0] = Vector2( ( ImGui::GetItemRectMin().x + ImGui::GetItemRectMax().x ) * 0.5f, ( ImGui::GetItemRectMin().y + ImGui::GetItemRectMax().y ) * 0.5f );
    ImGui::TableNextColumn();
    if( Button( "chart-area-line", "LODs", "Color the selected terrain by LOD. Levels 0 to 5: green, yellow, orange, blue, purple, pink.", terrain != nullptr, terrain && terrain->ShowLods ) ) terrain->ShowLods = !terrain->ShowLods;
    m_lodButtonCenters[1] = Vector2( ( ImGui::GetItemRectMin().x + ImGui::GetItemRectMax().x ) * 0.5f, ( ImGui::GetItemRectMin().y + ImGui::GetItemRectMax().y ) * 0.5f );
}


void TerrainWidget::DrawLodControls()
{
    auto* terrain = SelectedTerrain();
    const bool streaming = terrain && terrain->GetStreamingStats();
    if( !ImGui::CollapsingHeader( streaming ? "Terrain LODs##streamed" : "Terrain LODs##resident", streaming ? 0 : ImGuiTreeNodeFlags_DefaultOpen ) ) return;
    if( !terrain ) { ImGui::TextDisabled( "Select a terrain instance to inspect LODs." ); return; }
    ImGui::TextWrapped( "Cameras share the finest requested detail." );
    const auto& choices = terrain->GetPatchLods();
    std::array<size_t, 32> histogram{}; size_t stitched = 0;
    for( const auto& choice : choices ) { ++histogram[std::min<size_t>( choice.Level, 31 )]; stitched += choice.Edges != 0; }
    ImGui::Text( "Triangles: %llu / %llu", static_cast<unsigned long long>( terrain->GetLodTriangles() ), static_cast<unsigned long long>( terrain->GetFullDetailTriangles() ) );
    ImGui::Text( "%zu patches, %zu stitched", choices.size(), stitched );
    for( size_t level = 0; level < histogram.size(); ++level ) if( histogram[level] )
    { ImGui::Text( "Level %zu: %zu patches", level, histogram[level] ); }
    ImGui::Text( "Error budget: %.2f px", terrain->LodPixelError );
    ImGui::TextWrapped( "Tune error and hysteresis in the Terrain inspector." );
    int maximum = 0;
    if( auto data = terrain->GetData() ) for( const auto& patch : data->Patches ) maximum = std::max( maximum, patch->BaseLod + static_cast<int>( patch->Mesh.Lods.size() ) - 1 );
    ImGui::SliderInt( "Force level", &terrain->ForcedLod, -1, maximum );
    ImGui::TextWrapped( streaming ? "-1 = Auto. Forced levels respect available streamed detail." : "-1 = Auto. Forced levels bypass the error budget." );
    if( !terrain->GetLodError().empty() )
    {
        ImGui::PushStyleColor( ImGuiCol_Text, ImGuiTheme::ErrorText() );
        ImGui::TextWrapped( "%s", terrain->GetLodError().c_str() );
        ImGui::PopStyleColor();
    }
}
#endif
