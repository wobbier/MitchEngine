#include "TerrainFileAutomation.h"
#if USING( ME_EDITOR )
#include "TerrainWidget.h"
#include "TerrainAssetActions.h"
#include "Widgets/AssetBrowser.h"
#include "EditorApp.h"
#include "Havana.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Editor/EditorOperations.h"
#include "Editor/UndoStack.h"
#include "Components/Graphics/Terrain.h"
#include "Terrain/TerrainAsset.h"
#include "World/SceneSerializer.h"
#include "Resource/AssetDatabase.h"
#include "Utils/GUID.h"
#include "Path.h"
#include "imgui.h"
#include <fstream>
#include <cstdio>

namespace
{
    std::filesystem::path s_root;
    std::string s_original, s_current, s_copy, s_trash;
    TerrainDocument* s_document = nullptr;
    uint64_t s_guid = 0;
    bool SourceAt( const std::string& path, uint64_t guid, float height, std::string& error )
    {
        TerrainData::Heightfield field; uint64_t actual; std::string text;
        if( !TerrainData::LoadSource( Path( path ).FullPath, field, actual, text, error ) ) return false;
        float sample;
        if( actual != guid || field.GetSample( { 0, 0 }, sample ) != TerrainData::QueryStatus::Ready || sample != height )
        { error = "Terrain file identity or sample changed unexpectedly"; return false; }
        return true;
    }
}


bool TerrainFileAutomation::Execute( const std::string& command, const std::string& args, std::string& error )
{
    auto* widget = TerrainWidget::Get();
    auto* app = static_cast<EditorApp*>( GetEngine().GetGame() );
    auto* browser = app->Editor->GetAssetBrowser();
    if( !widget || !browser ) return false;
    if( command == "terrain-file-fixture" )
    {
        s_root = std::filesystem::path( Path( ".tmp/Tests/TerrainFileFlows" ).FullPath ) / std::to_string( GUID::Generate() );
        std::filesystem::create_directories( s_root / "Move" );
        s_original = s_current = ( s_root / "Original.terrain" ).generic_string();
        TerrainData::Settings s; s.TileQuads = 4; s.PatchQuads = 2; s.MinTile = { -1, -1 }; s.TilesX = s.TilesZ = 2;
        s_document = widget->Create( s_current, s );
        if( !s_document ) return false;
        s_guid = s_document->GetGUID();
        return widget->ConfigureRecovery( s_root / "Recovery" );
    }
    if( command == "terrain-file-duplicate" )
    {
        browser->m_selection = { s_current }; browser->DuplicateSelection();
        s_copy = ( std::filesystem::path( s_current ).parent_path() / "Original Copy.terrain" ).generic_string();
        const auto guid = AssetDatabase::Get().FindGUID( Path( s_copy ).GetLocalPathString() );
        if( !guid || guid == s_guid ) { error = "Terrain duplicate did not receive a fresh GUID"; return false; }
        return SourceAt( s_copy, guid, 0.f, error ); // Browser duplicates the saved asset, not the authoring preview.
    }
    if( command == "terrain-file-rename" )
    {
        browser->BeginRename( s_current );
        std::snprintf( browser->m_renameBuffer, sizeof( browser->m_renameBuffer ), "%s", args.c_str() );
        browser->CommitRename();
        s_current = ( std::filesystem::path( s_current ).parent_path() / args ).generic_string();
    }
    else if( command == "terrain-file-move" )
    {
        browser->m_draggedPaths = { s_current }; browser->MoveSelectionTo( ( s_root / "Move" ).generic_string() );
        s_current = ( s_root / "Move" / std::filesystem::path( s_current ).filename() ).generic_string();
    }
    else if( command == "terrain-file-folder" )
    {
        browser->BeginRename( ( s_root / "Move" ).generic_string() );
        std::snprintf( browser->m_renameBuffer, sizeof( browser->m_renameBuffer ), "%s", "MovedFolder" );
        browser->CommitRename();
        s_current = ( s_root / "MovedFolder" / std::filesystem::path( s_current ).filename() ).generic_string();
    }
    else if( command == "terrain-file-assert-moved" )
    {
        if( widget->GetActive() != s_document || Path( s_current ).GetLocalPathString() != s_document->GetPath() ||
            AssetDatabase::Get().FindPath( s_guid ) != Path( s_current ).GetLocalPathString() || std::filesystem::exists( s_original ) )
        { error = "Moved terrain document or GUID path did not follow the file operation: document=" + s_document->GetPath() + ", database=" + AssetDatabase::Get().FindPath( s_guid ) + ", expected=" + Path( s_current ).GetLocalPathString(); return false; }
        return SourceAt( s_current, s_guid, std::stof( args ), error );
    }
    else if( command == "terrain-file-assert-recovery" )
    {
        for( const auto& entry : std::filesystem::directory_iterator( s_root / "Recovery" ) )
        {
            TerrainData::RecoveryInfo info; TerrainData::Heightfield field;
            if( !TerrainData::ReadRecovery( entry.path(), info, field, error ) ) return false;
            if( info.AssetGUID != s_guid ) continue;
            float height;
            if( Path( info.AssetPath ).FullPath != Path( s_current ).FullPath || field.GetSample( { 0, 0 }, height ) != TerrainData::QueryStatus::Ready || height != 2.f )
            { error = "Recovery did not follow moved terrain edits"; return false; }
            return true;
        }
        error = "Moved terrain recovery is missing"; return false;
    }
    else if( command == "terrain-file-request-trash" )
    {
        browser->m_pendingDelete = { s_current }; browser->m_openDeleteConfirmation = true; browser->IsOpen = true;
        return true;
    }
    else if( command == "terrain-file-confirm" ) { ImGui::GetIO().AddKeyEvent( ImGuiKey_Enter, args == "true" ); return true; }
    else if( command == "terrain-file-assert-error" )
    {
        if( browser->m_fileOperationError.find( "Save or revert" ) == std::string::npos || !s_document->IsDirty() || !std::filesystem::exists( s_current ) )
        { error = "The browser did not show why unsaved terrain cannot be trashed"; return false; }
        return true;
    }
    else if( command == "terrain-file-reject-trash" )
    {
        std::string target, rejected;
        if( TerrainAssetActions::Trash( args == "folder" ? ( s_root / "MovedFolder" ).generic_string() : s_current, target, rejected ) || rejected.empty() || !s_document->IsDirty() )
        { error = "Trash did not protect unsaved terrain edits"; return false; }
        return true;
    }
    else if( command == "terrain-file-rescan-refs" )
    {
        const auto old = Path( s_original ).GetLocalPathString(), current = Path( s_current ).GetLocalPathString();
        json data = { { "TerrainSource", old }, { "AssetReferences", json::object() } };
        data["AssetReferences"][SceneSerializer::GUIDToString( s_guid )] = old;
        AssetDatabase::Get().Unregister( current );
        AssetDatabase::Get().Refresh( { s_root.generic_string() } );
        if( SceneSerializer::RemapAssetReferences( data, "Terrain file fixture" ) == 0 || data["TerrainSource"] != current )
        { error = "Saved terrain GUID references did not follow the move after a metadata scan"; return false; }
        return true;
    }
    else if( command == "terrain-file-alias" )
    {
        auto entity = EditorOps::GetWorld().FindEntityByName( "Terrain" );
        if( !entity ) return false;
        entity->GetComponent<Terrain>().Asset = Path( s_original );
        return widget->Open( s_original ) == s_document;
    }
    else if( command == "terrain-file-trash" )
    {
        if( !TerrainAssetActions::Trash( s_current, s_trash, error ) ) return false;
        if( widget->GetActive() == s_document || std::filesystem::exists( s_current ) || !AssetDatabase::Get().FindPath( s_guid ).empty() )
        { error = "Trashed terrain document or identity stayed live"; return false; }
        return SourceAt( s_trash, s_guid, 2.f, error );
    }
    else if( command == "terrain-file-reopen-copy" ) return widget->Open( s_copy ) != nullptr;
    else if( command == "terrain-file-external" )
    {
        TerrainData::Heightfield field; uint64_t guid; std::string text, changed;
        return TerrainData::LoadSource( s_current, field, guid, text, error ) && field.SetSample( { 0, 0 }, 9.f, error ) &&
            TerrainData::SaveSource( s_current, field, guid, &text, changed, error );
    }
    else if( command == "terrain-file-save-conflict" )
    {
        if( s_document->Save() || !s_document->IsDirty() ) { error = "Moving cleared an external source conflict"; return false; }
        return SourceAt( s_current, s_guid, 9.f, error );
    }
    else { error = "Unknown terrain file command"; return false; }
    return std::filesystem::exists( s_current );
}
#endif
