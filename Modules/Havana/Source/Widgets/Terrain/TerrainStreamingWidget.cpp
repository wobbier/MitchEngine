#include "TerrainWidget.h"
#if USING( ME_EDITOR )
#include "Editor/Selection.h"
#include "Components/Graphics/Terrain.h"
#include "Terrain/TerrainStreamCache.h"
#include "Events/EditorEvents.h"
#include "Path.h"
#include "misc/cpp/imgui_stdlib.h"
#include "ImGuiTheme.h"
#include <algorithm>
#include <bit>
#include <chrono>

bool TerrainWidget::StartStreamCook( const std::string& path, uint8_t base )
{
    if( !m_active || m_cook.valid() || IsPlaying() ) return false;
    m_active->EndStroke();
    auto field = m_active->GetField(); const uint64_t guid = m_active->GetGUID(); const auto output = std::filesystem::path( Path( path ).FullPath );
    m_cookMessage = "Cooking " + output.generic_string();
    try
    {
        m_cook = std::async( std::launch::async, [field = std::move( field ), guid, output, base]() {
            std::string error; if( !TerrainData::CookStreamArchive( output, field, guid, base, error ) ) return error;
            return std::string();
        } );
    }
    catch( const std::exception& e ) { m_cookMessage = e.what(); return false; }
    return true;
}


bool TerrainWidget::FinishStreamCook( bool wait )
{
    if( !m_cook.valid() ) return true;
    if( !wait && m_cook.wait_for( std::chrono::seconds( 0 ) ) != std::future_status::ready ) return false;
    std::string error;
    try { error = m_cook.get(); } catch( const std::exception& e ) { error = e.what(); }
    m_cookMessage = error.empty() ? "Streamed terrain cooked. Assign the .terrainc to a Terrain component to preview streaming." : "Cook failed: " + error;
    return error.empty();
}


void TerrainWidget::StreamCookDialog()
{
    if( !m_active ) return;
    m_cookRequested = true; m_replaceCook = false;
    auto path = std::filesystem::path( m_active->GetPath() ); path.replace_extension( ".terrainc" ); m_cookPath = path.generic_string();
    m_coverageLod = std::min( 3, int( std::bit_width( m_active->GetField().GetSettings().PatchQuads ) - 1 ) );
}


void TerrainWidget::DrawStreamCook()
{
    if( m_cookRequested ) { ImGui::OpenPopup( "Cook Streamed Terrain" ); m_cookRequested = false; }
    if( !ImGui::BeginPopupModal( "Cook Streamed Terrain", nullptr, ImGuiWindowFlags_AlwaysAutoResize ) ) return;
    ImGui::SetNextItemWidth( ImGuiTheme::Em( 30 ) ); ImGui::InputText( "Cooked .terrainc", &m_cookPath );
    if( ImGui::Button( "Choose Output..." ) )
    {
        RequestAssetSelectionEvent event( [this]( Path path ) { m_cookPath = path.GetLocalPathString(); m_cookRequested = true; }, AssetType::Unknown, true ); event.Fire(); ImGui::CloseCurrentPopup();
    }
    const int maximum = m_active ? int( std::bit_width( m_active->GetField().GetSettings().PatchQuads ) - 1 ) : 0;
    ImGui::SliderInt( "Coverage level", &m_coverageLod, 0, maximum );
    ImGui::PushTextWrapPos( ImGuiTheme::Em( 34 ) );
    ImGui::TextWrapped( "Higher coverage levels use fewer resident vertices while detail streams. Cooking uses a snapshot of the current heights, including unsaved strokes." );
    ImGui::TextWrapped( "Runtime tiles support up to 256 quads. Larger overviews need a coarser cook or separate datasets." );
    ImGui::PopTextWrapPos();
    const bool exists = std::filesystem::exists( Path( m_cookPath ).FullPath );
    if( exists ) ImGui::Checkbox( "Replace the existing cooked file", &m_replaceCook );
    ImGui::BeginDisabled( !m_active || IsCooking() || IsPlaying() || ( exists && !m_replaceCook ) );
    if( ImGui::Button( "Cook" ) && StartStreamCook( m_cookPath, static_cast<uint8_t>( m_coverageLod ) ) ) ImGui::CloseCurrentPopup();
    ImGui::EndDisabled(); ImGui::SameLine(); if( ImGui::Button( "Cancel" ) ) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}


void TerrainWidget::DrawResidencyButton()
{
    const auto entity = Selection::Get().GetActive(); auto* terrain = entity ? entity->TryGetComponent<Terrain>() : nullptr;
    ImGui::TableNextColumn();
    if( Button( "refresh", "Residency", "Green patches have uploaded detail; blue patches use coarse coverage. Inspect below for reserved memory, pending requests and collision readiness.", terrain && terrain->GetStreamingStats(), terrain && terrain->ShowResidency ) ) terrain->ShowResidency = !terrain->ShowResidency;
}


void TerrainWidget::DrawStreamingControls()
{
    const auto entity = Selection::Get().GetActive(); auto* terrain = entity ? entity->TryGetComponent<Terrain>() : nullptr;
    const auto* stats = terrain ? terrain->GetStreamingStats() : nullptr;
    if( !stats ) return;
    ImGui::SeparatorText( "Streaming" );
    ImGui::Text( "%zu resident / %zu detail / %zu retiring", stats->Resident, stats->Detail, stats->Retiring );
    if( stats->Retiring ) ImGui::Text( "Retiring pins: %zu query / %zu draw / %zu collision", stats->QueryPins, stats->DrawPins, stats->CollisionPins );
    ImGui::Text( "%zu pending  /  worker %s", stats->Pending, stats->Loading ? "loading" : "idle" );
    constexpr double mib = 1024.0 * 1024;
    ImGui::Text( "CPU reserved: %.2f / %d MiB", stats->CpuReserved / mib, terrain->CpuBudgetMiB );
    ImGui::Text( "GPU reserved: %.2f / %d MiB", stats->GpuReserved / mib, terrain->GpuBudgetMiB );
    ImGui::Text( "Collision reserved: %.2f / %d MiB", stats->CollisionReserved / mib, terrain->CollisionBudgetMiB );
    ImGui::Text( "Collision pages: %s", stats->CollisionReady ? "ready" : "waiting" );
    ImGui::Text( "Loaded %llu / evicted %llu / cancelled %llu", static_cast<unsigned long long>( stats->Loaded ), static_cast<unsigned long long>( stats->Evicted ), static_cast<unsigned long long>( stats->Cancelled ) );
    ImGui::TextWrapped( "Reservations include pending work and retiring consumers. Tune radii, memory and upload limits in the Terrain inspector." );
    if( !stats->Pressure.empty() ) ImGui::TextWrapped( "%s", stats->Pressure.c_str() );
    if( !stats->Error.empty() ) { ImGui::PushStyleColor( ImGuiCol_Text, ImGuiTheme::ErrorText() ); ImGui::TextWrapped( "%s", stats->Error.c_str() ); ImGui::PopStyleColor(); }
}
#endif
