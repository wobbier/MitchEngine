#include "TerrainAssetActions.h"
#if USING( ME_EDITOR )
#include "TerrainWidget.h"
#include "Terrain/TerrainFileOperations.h"
#include "Editor/EditorOperations.h"
#include "Editor/UndoStack.h"
#include "Components/Graphics/Terrain.h"
#include "Engine/World.h"
#include "Resource/AssetDatabase.h"
#include "Resource/ResourceCache.h"
#include "Utils/GUID.h"
#include "Utils/PlatformUtils.h"
#include "Path.h"
#include "CLog.h"
#include <fstream>

namespace
{
    std::string Key( const std::string& path ) { return std::filesystem::path( Path( path ).FullPath ).lexically_normal().generic_string(); }
    bool Within( const std::string& path, const std::string& root ) { return path == root || path.rfind( root + "/", 0 ) == 0; }
    std::string Remap( const std::string& path, const std::string& from, const std::string& to )
    {
        return Path( to + Key( path ).substr( from.size() ) ).GetLocalPathString();
    }
    void UpdateInstances( const std::string& from, const std::string& to )
    {
        bool changed = false;
        EditorOps::GetWorld().ForEachEntity( [&]( Entity& entity ) {
            auto* terrain = entity.TryGetComponent<Terrain>();
            if( !terrain || terrain->Asset.GetLocalPathString().empty() || !Within( Key( terrain->Asset.FullPath ), from ) ) return;
            if( !to.empty() ) { terrain->Asset = Path( Remap( terrain->Asset.FullPath, from, to ) ); changed = true; }
            terrain->ReloadAsset();
        } );
        if( changed ) UndoStack::Get().MarkDirty();
    }
}


bool TerrainWidget::CanMoveAssets( const std::string& source, const std::string& destination, std::string& error ) const
{
    const auto from = Key( source ), to = Key( destination );
    for( const auto& document : m_documents ) if( Within( Key( document->GetPath() ), from ) )
    {
        if( IsPlaying() ) { error = "Stop Play before moving an open terrain asset"; return false; }
        const auto moved = Key( Remap( document->GetPath(), from, to ) );
        for( const auto& other : m_documents )
            if( other != document && Key( other->GetPath() ) == moved ) { error = "Close the terrain document already open at the destination"; return false; }
    }
    return true;
}


bool TerrainWidget::CanRemoveAssets( const std::string& path, std::string& error ) const
{
    for( const auto& document : m_documents ) if( Within( Key( document->GetPath() ), Key( path ) ) )
    {
        if( IsPlaying() ) { error = "Stop Play before moving an open terrain asset or folder to Trash"; return false; }
        if( document->IsDirty() ) { error = "Save or revert open terrain edits before moving this asset or folder to Trash"; return false; }
    }
    return true;
}


void TerrainWidget::OnAssetsMoved( const std::string& source, const std::string& destination, const std::string& before, const std::string& after )
{
    const auto from = Key( source ), to = Key( destination );
    // Path retains absolute identities outside asset roots (for example imported/test workspaces).
    // The browser's project-relative folder move handles normal Assets roots separately.
    AssetDatabase::Get().Move( from, to );
    ScanRecoveries();
    for( const auto& entry : m_recoveries ) if( !entry.Info.AssetPath.empty() && Within( Key( entry.Info.AssetPath ), from ) )
    {
        TerrainData::RecoveryInfo info; TerrainData::Heightfield field; std::string error;
        if( !TerrainData::ReadRecovery( entry.File, info, field, error ) ) { m_error = "Asset moved; recovery was retained at its old path: " + error; continue; }
        info.AssetPath = Remap( info.AssetPath, from, to );
        if( !before.empty() && info.BaseManifest == before ) info.BaseManifest = after;
        if( !TerrainData::WriteRecovery( entry.File, info, field, error ) ) m_error = "Asset moved; could not update a recovery snapshot: " + error;
    }
    for( auto& document : m_documents ) if( Within( Key( document->GetPath() ), from ) )
    {
        const auto old = document->GetPath();
        document->Relocate( Remap( old, from, to ), before, after );
        ResourceCache::GetInstance().Evict( Path( old ).FullPath );
    }
    UpdateInstances( from, to );
    if( AutomaticRecoveryEnabled() ) WriteRecoveries(); else ScanRecoveries();
}


void TerrainWidget::OnAssetsRemoved( const std::string& path )
{
    const auto root = Key( path );
    for( auto it = m_documents.begin(); it != m_documents.end(); )
    {
        if( !Within( Key( ( *it )->GetPath() ), root ) ) { ++it; continue; }
        ResourceCache::GetInstance().Evict( Path( ( *it )->GetPath() ).FullPath );
        m_recoveryDirtyDocuments.erase( it->get() );
        if( m_revertTarget == it->get() ) m_revertTarget = nullptr;
        if( m_active == it->get() ) m_active = nullptr;
        it = m_documents.erase( it );
    }
    if( !m_active && !m_documents.empty() ) m_active = m_documents.front().get();
    UpdateInstances( root, {} ); ScanRecoveries();
}


namespace TerrainAssetActions
{
    bool IsSource( const std::string& path ) { return std::filesystem::path( path ).extension() == ".terrain"; }

    bool Move( const std::string& from, const std::string& to, std::string& error )
    {
        auto* widget = TerrainWidget::Get();
        if( widget && !widget->CanMoveAssets( from, to, error ) ) return false;
        TerrainData::SourceTransfer transfer;
        if( !TerrainData::TransferSource( Path( from ).FullPath, Path( to ).FullPath, false, transfer, error ) ) return false;
        AssetDatabase::Get().Register( Path( from ).GetLocalPathString(), transfer.AssetGUID );
        AssetDatabase::Get().Move( Path( from ).GetLocalPathString(), Path( to ).GetLocalPathString() );
        ResourceCache::GetInstance().Evict( Path( from ).FullPath );
        if( widget ) widget->OnAssetsMoved( from, to, transfer.BeforeManifest, transfer.AfterManifest );
        return true;
    }

    bool Duplicate( const std::string& from, const std::string& to, std::string& error )
    {
        TerrainData::SourceTransfer transfer;
        if( !TerrainData::TransferSource( Path( from ).FullPath, Path( to ).FullPath, true, transfer, error ) ) return false;
        AssetDatabase::Get().Register( Path( to ).GetLocalPathString(), transfer.AssetGUID );
        return true;
    }

    bool Trash( const std::string& path, std::string& out, std::string& error )
    {
        auto* widget = TerrainWidget::Get();
        if( widget && !widget->CanRemoveAssets( path, error ) ) return false;
        if( !IsSource( path ) )
        {
            // Its own folder, so the Trash (ED-13) can restore it and its .meta by name.
            const auto directory = std::filesystem::path( Path( ".tmp/Trash" ).FullPath ) / ( "Asset-" + std::to_string( EngineGUID::Generate() ) );
            const auto destination = directory / std::filesystem::path( Path( path ).FullPath ).filename();
            std::error_code failed;
            std::filesystem::create_directories( directory, failed );
            std::filesystem::rename( Path( path ).FullPath, destination, failed );
            if( failed ) { error = "Could not move asset to Trash: " + failed.message(); std::filesystem::remove( directory, failed ); return false; }
            if( std::filesystem::exists( Path( path ).FullPath + ".meta" ) ) std::filesystem::rename( Path( path ).FullPath + ".meta", destination.generic_string() + ".meta", failed );
            AssetDatabase::Get().Unregister( Path( path ).GetLocalPathString() );
            if( widget ) widget->OnAssetsRemoved( path );
            out = destination.generic_string(); return true;
        }
        const auto directory = std::filesystem::path( Path( ".tmp/Trash" ).FullPath ) / ( "Terrain-" + std::to_string( EngineGUID::Generate() ) );
        try { std::filesystem::create_directories( directory ); }
        catch( const std::exception& e ) { error = e.what(); return false; }
        const auto destination = directory / std::filesystem::path( path ).filename();
        TerrainData::SourceTransfer transfer;
        if( !TerrainData::TransferSource( Path( path ).FullPath, destination, false, transfer, error ) )
        { std::error_code ignored; std::filesystem::remove( directory, ignored ); return false; }
        AssetDatabase::Get().Unregister( Path( path ).GetLocalPathString() );
        ResourceCache::GetInstance().Evict( Path( path ).FullPath );
        if( widget ) widget->OnAssetsRemoved( path );
        out = destination.generic_string(); return true;
    }
}
#endif
