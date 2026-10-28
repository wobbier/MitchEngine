#include "TerrainWidget.h"
#if USING( ME_EDITOR )
#include "Editor/EditorActions.h"
#include "Engine/AutomationRunner.h"
#include "Utils/EditorConfig.h"
#include "Utils/GUID.h"
#include "Path.h"
#include "ImGuiTheme.h"
#include <algorithm>
#include <cmath>
#include <ctime>

namespace
{
    std::string RecoveryKey( const std::string& path ) { return std::filesystem::path( Path( path ).FullPath ).lexically_normal().generic_string(); }
    std::string RecoveryTime( uint64_t value )
    {
        const std::time_t time = static_cast<std::time_t>( value );
        std::tm utc{};
#if USING( ME_PLATFORM_WINDOWS )
        if( gmtime_s( &utc, &time ) != 0 ) return "Unknown time";
#else
        if( !gmtime_r( &time, &utc ) ) return "Unknown time";
#endif
        char text[40]; std::strftime( text, sizeof( text ), "%Y-%m-%d %H:%M:%S UTC", &utc ); return text;
    }
}

void TerrainWidget::InitRecovery()
{
    m_recoverySession = Guid::Generate();
    // Unattended runs opt into their own test directory explicitly and never inspect a user's recovery.
    if( !AutomationRunner::IsUnattendedRun() )
    {
        ConfigureRecovery( Path( ".tmp/Autosave/Terrain" ).FullPath );
    }
    auto& actions = EditorActions::Get();
    actions.Register( { "Terrain.Recovery", "Terrain Recovery...", "Terrain", 0, 0, [this]() { ScanRecoveries(); m_showRecoveryRequested = true; } } );
    actions.Register( { "Terrain.Snapshot", "Save Terrain Recovery Now", "Terrain", 0, 0, [this]() {
        if( m_active ) m_active->EndStroke(); WriteRecoveries();
    }, [this]() { return !IsPlaying() && HasUnsaved() && !m_recoveryDirectory.empty(); } } );
}

bool TerrainWidget::ConfigureRecovery( const std::filesystem::path& directory )
{
    try
    {
        if( directory.empty() || ( std::filesystem::exists( directory ) && !std::filesystem::is_directory( directory ) ) )
        { m_error = "Choose a directory for terrain recovery snapshots"; return false; }
        m_recoveryDirectory = std::filesystem::absolute( directory ).lexically_normal();
        if( !m_recoverySession ) m_recoverySession = Guid::Generate();
        m_nextRecoveryAt = ImGui::GetTime() + 120; ScanRecoveries();
        return true;
    }
    catch( const std::exception& e ) { m_error = e.what(); return false; }
}

bool TerrainWidget::AutomaticRecoveryEnabled() const
{
    const float interval = EditorConfig::GetInstance().GetPreference<float>( "Autosave.IntervalSeconds", 120.f );
    return !m_recoveryDirectory.empty() && EditorConfig::GetInstance().GetPreference<bool>( "Terrain.Autosave", true ) && std::isfinite( interval ) && interval > 0;
}

void TerrainWidget::ScanRecoveries()
{
    std::vector<RecoveryEntry> entries;
    if( m_recoveryDirectory.empty() ) { m_recoveries.clear(); return; }
    try
    {
        if( std::filesystem::exists( m_recoveryDirectory ) )
        {
            for( const auto& file : std::filesystem::directory_iterator( m_recoveryDirectory ) )
            {
                if( file.path().extension() != ".terrain-recovery" ) continue;
                RecoveryEntry entry; entry.File = file.path();
                std::error_code statError;
                if( !file.is_regular_file( statError ) )
                {
                    if( !statError || statError == std::errc::no_such_file_or_directory ) continue;
                    entry.Error = statError.message();
                }
                else TerrainData::InspectRecovery( entry.File, entry.Info, entry.Error );
                // The list only retains identity/time. Restore validates and reads the complete file.
                std::string{}.swap( entry.Info.BaseManifest ); entries.push_back( std::move( entry ) );
            }
        }
        std::sort( entries.begin(), entries.end(), []( const RecoveryEntry& a, const RecoveryEntry& b ) {
            return a.Info.SavedAt != b.Info.SavedAt ? a.Info.SavedAt > b.Info.SavedAt : a.File.generic_string() < b.File.generic_string();
        } );
        m_recoveries = std::move( entries );
    }
    catch( const std::exception& e ) { m_error = std::string( "Could not list terrain recovery snapshots: " ) + e.what(); }
}

bool TerrainWidget::WriteRecoveries()
{
    if( m_recoveryDirectory.empty() || IsPlaying() ) return false;
    bool success = true, deferred = false;
    for( auto& document : m_documents )
    {
        if( document->IsStroking() ) { deferred = true; continue; } // Never serialize a partially captured gesture.
        if( !document->IsDirty() ) { document->ClearRecovery(); continue; }
        const auto name = std::to_string( document->GetGUID() ) + "-" + std::to_string( m_recoverySession ) + ".terrain-recovery";
        if( !document->SaveRecovery( m_recoveryDirectory / name ) ) { m_error = document->GetError(); success = false; }
    }
    if( success ) m_recoveryMessage = deferred ? "The active stroke will be backed up after release." : "Terrain recovery snapshots are up to date.";
    ScanRecoveries(); return success && !deferred;
}

bool TerrainWidget::PrepareForPlay()
{
    EndFoliageStroke();
    for( auto& document : m_documents ) document->EndStroke();
    for( auto& document : m_documents ) if( !document->WaitForPreview() )
    { m_error = document->GetError(); IsOpen = true; m_focusRequested = true; return false; }
    if( AutomaticRecoveryEnabled() ) WriteRecoveries();
    return true;
}

void TerrainWidget::UpdateRecovery()
{
    for( auto& document : m_documents )
    {
        if( document->IsDirty() ) m_recoveryDirtyDocuments.insert( document.get() );
        else if( m_recoveryDirtyDocuments.erase( document.get() ) ) { document->ClearRecovery(); ScanRecoveries(); }
    }
    const float interval = EditorConfig::GetInstance().GetPreference<float>( "Autosave.IntervalSeconds", 120.f );
    const double now = ImGui::GetTime();
    if( !AutomaticRecoveryEnabled() || IsPlaying() ) { m_nextRecoveryAt = now + ( std::isfinite( interval ) && interval > 0 ? interval : 120 ); return; }
    if( m_nextRecoveryAt > now + interval ) m_nextRecoveryAt = now + interval;
    if( now < m_nextRecoveryAt ) return;
    m_nextRecoveryAt = now + interval;
    if( !WriteRecoveries() ) m_nextRecoveryAt = now + std::min( interval, 5.f );
}

bool TerrainWidget::RestoreAssetRecovery( const std::string& path )
{
    for( size_t i = 0; i < m_recoveries.size(); ++i )
        if( !m_recoveries[i].Info.AssetPath.empty() && RecoveryKey( m_recoveries[i].Info.AssetPath ) == RecoveryKey( path ) ) return RestoreRecovery( i );
    m_error = "No recovery snapshot found for that terrain asset"; return false;
}

bool TerrainWidget::RestoreRecovery( size_t index )
{
    if( IsPlaying() || index >= m_recoveries.size() ) { m_error = "Stop Play and select an available recovery snapshot"; return false; }
    const auto entry = m_recoveries[index];
    if( !entry.Error.empty() ) { m_error = entry.Error; return false; }
    for( auto& document : m_documents ) if( RecoveryKey( document->GetPath() ) == RecoveryKey( entry.Info.AssetPath ) )
    {
        if( !document->RestoreRecovery( entry.File ) ) { m_error = document->GetError(); return false; }
        if( m_active && m_active != document.get() ) m_active->EndStroke();
        m_active = document.get(); IsOpen = true; m_focusRequested = true; m_error.clear();
        m_recoveryMessage = "Recovered unsaved heights. Save or Save Copy to keep them."; return true;
    }
    auto document = std::make_unique<TerrainDocument>( entry.Info.AssetPath );
    if( !document->RestoreRecovery( entry.File ) ) { m_error = document->GetError(); return false; }
    if( m_active ) m_active->EndStroke();
    m_active = document.get(); m_documents.push_back( std::move( document ) );
    IsOpen = true; m_focusRequested = true; m_error.clear();
    m_recoveryMessage = "Recovered unsaved heights. Save or Save Copy to keep them."; return true;
}

bool TerrainWidget::DiscardRecovery( size_t index )
{
    if( index >= m_recoveries.size() ) return false;
    std::error_code error; std::filesystem::remove( m_recoveries[index].File, error );
    if( error ) { m_error = error.message(); return false; }
    m_recoveryMessage = "Snapshot discarded. Open document edits are unchanged.";
    ScanRecoveries(); return true;
}

void TerrainWidget::DrawRecovery()
{
    if( m_showRecoveryRequested && !ImGui::IsPopupOpen( nullptr, ImGuiPopupFlags_AnyPopupId ) )
    { ImGui::OpenPopup( "Terrain Recovery" ); m_showRecoveryRequested = false; }
    ImGui::SetNextWindowSize( ImVec2( ImGuiTheme::Em( 37 ), ImGuiTheme::Em( 25 ) ), ImGuiCond_FirstUseEver );
    if( !ImGui::BeginPopupModal( "Terrain Recovery", nullptr ) ) return;
    ImGui::TextWrapped( "Restore unsaved terrain heights from a previous session. Restored documents retain source conflict checks when saving." );
    if( ImGui::Button( "Refresh" ) ) ScanRecoveries();
    ImGui::SameLine(); ImGui::TextDisabled( "%zu snapshots", m_recoveries.size() );
    if( !m_error.empty() ) ImGui::TextWrapped( "%s", m_error.c_str() );
    const float footer = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;
    ImGui::BeginChild( "Recovery list", ImVec2( 0, -footer ) );
    if( m_recoveries.empty() ) ImGui::TextDisabled( "No unsaved terrain snapshots." );
    size_t restore = m_recoveries.size(), discard = m_recoveries.size();
    for( size_t i = 0; i < m_recoveries.size(); ++i )
    {
        const auto& entry = m_recoveries[i]; ImGui::PushID( static_cast<int>( i ) );
        ImGui::Separator();
        ImGui::TextWrapped( "%s", entry.Info.AssetPath.empty() ? entry.File.filename().string().c_str() : entry.Info.AssetPath.c_str() );
        if( !entry.Error.empty() ) ImGui::TextColored( ImGuiTheme::ErrorText(), "%s", entry.Error.c_str() );
        else ImGui::TextDisabled( "%s", RecoveryTime( entry.Info.SavedAt ).c_str() );
        ImGui::BeginDisabled( IsPlaying() || !entry.Error.empty() );
        if( ImGui::Button( "Restore", ImGuiTheme::DialogButtonSize() ) ) restore = i;
        ImGui::EndDisabled();
        ImGui::SameLine();
        if( ImGui::Button( "Discard...", ImGuiTheme::DialogButtonSize() ) ) ImGui::OpenPopup( "Discard snapshot?" );
        if( ImGui::BeginPopupModal( "Discard snapshot?", nullptr, ImGuiWindowFlags_AlwaysAutoResize ) )
        {
            ImGui::TextUnformatted( "Delete this recovery snapshot?" );
            ImGui::TextDisabled( "Open document edits will remain." );
            if( ImGui::Button( "Discard", ImGuiTheme::DialogButtonSize() ) ) { discard = i; ImGui::CloseCurrentPopup(); }
            ImGui::SameLine(); if( ImGui::Button( "Cancel", ImGuiTheme::DialogButtonSize() ) ) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }
    ImGui::EndChild();
    if( ImGui::Button( "Later", ImGuiTheme::DialogButtonSize() ) || ImGui::IsKeyPressed( ImGuiKey_Escape ) ) ImGui::CloseCurrentPopup();
    if( restore < m_recoveries.size() && RestoreRecovery( restore ) ) ImGui::CloseCurrentPopup();
    if( discard < m_recoveries.size() ) DiscardRecovery( discard );
    ImGui::EndPopup();
}
#endif
