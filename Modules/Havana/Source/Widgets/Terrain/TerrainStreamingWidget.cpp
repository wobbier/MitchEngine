#include "TerrainWidget.h"
#if USING( ME_EDITOR )
#include "Editor/Selection.h"
#include "Components/Graphics/Terrain.h"
#include "Terrain/TerrainStreamCache.h"
#include "Cores/TerrainCore.h"
#include "Editor/EditorOperations.h"
#include "Engine/ProjectSettings.h"
#include "Engine/World.h"
#include "Events/EditorEvents.h"
#include "Path.h"
#include "misc/cpp/imgui_stdlib.h"
#include "ImGuiTheme.h"
#include <algorithm>
#include <bit>
#include <chrono>

bool TerrainWidget::StartStreamCook( const std::string& path, uint8_t base, bool paged, bool compressed, bool hierarchical )
{
    if( !m_active || m_cook.valid() || IsPlaying() || ( compressed && !paged ) || ( hierarchical && ( !paged || !compressed ) ) ) return false;
    m_active->EndStroke();
    auto field = m_active->GetField(); const uint64_t guid = m_active->GetGUID(); const auto output = std::filesystem::path( Path( path ).FullPath );
    m_cookMessage = "Cooking " + output.generic_string();
    try
    {
        m_cook = std::async( std::launch::async, [field = std::move( field ), guid, output, base, paged, compressed, hierarchical]() {
            std::string error; if( !( hierarchical ? TerrainData::CookHierarchicalStreamArchive( output, field, guid, base, error ) :
                ( compressed ? TerrainData::CookCompressedStreamArchive( output, field, guid, base, error ) :
                ( paged ? TerrainData::CookPagedStreamArchive( output, field, guid, base, error ) : TerrainData::CookStreamArchive( output, field, guid, base, error ) ) ) ) ) return error;
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
    if( maximum < 1 ) m_hierarchicalCook = false;
    ImGui::BeginDisabled( maximum < 1 ); ImGui::Checkbox( "Hierarchical distant coverage", &m_hierarchicalCook ); ImGui::EndDisabled();
    if( m_hierarchicalCook ) { m_pagedCook = m_compressedCook = true; m_coverageLod = std::min( m_coverageLod, maximum - 1 ); }
    ImGui::SliderInt( "Coverage level", &m_coverageLod, 0, m_hierarchicalCook ? maximum - 1 : maximum );
    const bool layered = m_active && bool( m_active->GetField().GetSurface() );
    if( layered ) m_pagedCook = true;
    ImGui::BeginDisabled( layered || m_hierarchicalCook ); ImGui::Checkbox( "Page coverage", &m_pagedCook ); ImGui::EndDisabled();
    ImGui::BeginDisabled( !m_pagedCook || m_hierarchicalCook ); ImGui::Checkbox( "Compress pages", &m_compressedCook ); ImGui::EndDisabled();
    if( layered ) ImGui::TextWrapped( "Painted terrain includes layer settings and full-resolution weights in paged coverage." );
    ImGui::PushTextWrapPos( ImGuiTheme::Em( 34 ) );
    ImGui::TextWrapped( "Higher coverage levels use fewer resident vertices while detail streams. Cooking uses a snapshot of the current heights and paint, including unsaved strokes." );
    ImGui::TextWrapped( "Paged coverage loads a finite visual horizon around rendering cameras. Runtime tiles support up to 256 quads. Disable paging for the resident version 2 overview." );
    if( m_hierarchicalCook ) ImGui::TextWrapped( "A bounded resident root covers the complete terrain. Parent pages refine around cameras; distant painted weights use coarser samples." );
    if( m_pagedCook && m_compressedCook ) ImGui::TextWrapped( "Lossless page compression reduces disk traffic. Canonical heights, normals and painted weights remain exact; decoded residency still uses the configured budgets." );
    ImGui::PopTextWrapPos();
    const bool exists = std::filesystem::exists( Path( m_cookPath ).FullPath );
    if( exists ) ImGui::Checkbox( "Replace the existing cooked file", &m_replaceCook );
    ImGui::BeginDisabled( !m_active || IsCooking() || IsPlaying() || ( exists && !m_replaceCook ) );
    if( ImGui::Button( "Cook" ) && StartStreamCook( m_cookPath, static_cast<uint8_t>( m_coverageLod ), m_pagedCook, m_pagedCook && m_compressedCook, m_hierarchicalCook ) ) ImGui::CloseCurrentPopup();
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
    ImGui::Text( "Look-ahead: %zu CPU pages / %zu pending", stats->PrefetchResident, stats->PrefetchPending );
    if( terrain->GetData() && terrain->GetData()->PagedCoverage )
    {
        ImGui::Text( "Coverage: %zu pages / %zu pending / %zu retiring", stats->CoverageResident, stats->CoveragePending, stats->CoverageRetiring );
        ImGui::Text( "Horizon: %s / %.0f m", stats->HorizonReady ? "ready" : "loading", terrain->CoverageDistance );
        ImGui::Text( "Coverage loads %llu / evictions %llu", static_cast<unsigned long long>( stats->CoverageLoaded ), static_cast<unsigned long long>( stats->CoverageEvicted ) );
    }
    constexpr double mib = 1024.0 * 1024;
    ImGui::Text( "Page payloads: %.2f MiB read / %.2f MiB decoded", stats->BytesRead / mib, stats->DecodedBytesRead / mib );
    if( stats->HierarchyRoots ) ImGui::Text( "Hierarchy: %zu resident roots / %zu selected nodes", stats->HierarchyRoots, stats->HierarchySelected );
    if( const auto* core = EditorOps::GetWorld().GetCore<TerrainCore>() )
    {
        const auto& world = core->GetStreamingStats(); const auto& settings = ProjectSettings::Get();
        ImGui::Text( "World: %zu assets / %zu retiring / %zu loads", world.ActiveAssets, world.RetiringAssets, world.Loading );
        ImGui::Text( "World CPU: %.2f / %d MiB", world.CpuReserved / mib, settings.TerrainCpuBudgetMiB );
        ImGui::Text( "World GPU: %.2f / %d MiB", world.GpuReserved / mib, settings.TerrainGpuBudgetMiB );
        ImGui::Text( "World collision: %.2f / %d MiB", world.CollisionReserved / mib, settings.TerrainCollisionBudgetMiB );
        if( !world.Pressure.empty() ) ImGui::TextWrapped( "%s", world.Pressure.c_str() );
        ImGui::Separator();
    }
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
