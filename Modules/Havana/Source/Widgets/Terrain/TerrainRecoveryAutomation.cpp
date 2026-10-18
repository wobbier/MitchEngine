#include "TerrainRecoveryAutomation.h"
#if USING( ME_EDITOR )
#include "TerrainWidget.h"
#include "Terrain/TerrainAsset.h"
#include "JSON.h"
#include "Path.h"
#include "imgui_internal.h"
#include <cmath>
#include <fstream>
#include <sstream>

namespace TerrainRecoveryAutomation
{
    namespace
    {
        const std::filesystem::path& Root()
        {
            static const std::filesystem::path root = Path( ".tmp/Tests/TerrainRecoveryFlows/Restart" ).FullPath;
            return root;
        }
        bool Sample( const TerrainData::Heightfield& field, const std::string& args, std::string& error )
        {
            std::istringstream stream( args ); int64_t x, z; float expected, actual;
            if( !( stream >> x >> z >> expected ) || field.GetSample( { x, z }, actual ) != TerrainData::QueryStatus::Ready || std::abs( expected - actual ) > 0.0001f )
            { error = "Unexpected source/recovery sample"; return false; }
            return true;
        }
    }
    bool Execute( const std::string& command, const std::string& args, std::string& error )
    {
        auto* widget = TerrainWidget::Get();
        if( !widget ) { error = "Terrain panel is unavailable"; return false; }
        if( command == "terrain-recovery-reset" )
        {
            if( widget->HasUnsaved() ) { error = "Recovery fixture reset requires no open dirty documents"; return false; }
            std::error_code ec; std::filesystem::remove_all( Root(), ec );
            if( ec ) { error = ec.message(); return false; }
            return widget->ConfigureRecovery( Root() / "Snapshots" );
        }
        if( command == "terrain-recovery-configure" ) return widget->ConfigureRecovery( Root() / "Snapshots" );
        if( command == "terrain-recovery-create" )
        {
            if( args != "SourceA" && args != "SourceB" ) { error = "Expected SourceA or SourceB fixture"; return false; }
            TerrainData::Settings s; s.TileQuads = 4; s.PatchQuads = 2; s.TilesX = s.TilesZ = 2; s.MinTile = { -1, -1 };
            return widget->Create( ( Root() / ( args + ".terrain" ) ).generic_string(), s ) != nullptr;
        }
        if( command == "terrain-recovery-scan" ) { widget->ScanRecoveries(); return true; }
        if( command == "terrain-recovery-assert-count" )
        {
            widget->ScanRecoveries();
            if( widget->GetRecoveryCount() != static_cast<size_t>( std::stoul( args ) ) ) { error = "Unexpected terrain recovery snapshot count"; return false; }
            return true;
        }
        if( command == "terrain-recovery-restore" ) return widget->RestoreAssetRecovery( ( Root() / ( args + ".terrain" ) ).generic_string() );
        if( command == "terrain-recovery-save-now" ) return widget->WriteRecoveries();
        if( command == "terrain-recovery-discard" ) return widget->DiscardRecovery( static_cast<size_t>( std::stoul( args ) ) );
        if( command == "terrain-recovery-request-startup" ) { widget->RequestStartupRecovery(); return true; }
        if( command == "terrain-recovery-assert-popup" )
        {
            for( const auto& popup : ImGui::GetCurrentContext()->OpenPopupStack ) if( popup.Window && args == popup.Window->Name ) return true;
            error = "Expected popup is not open: " + args; return false;
        }
        auto* document = widget->GetActive();
        if( !document ) { error = "Open or restore a terrain document first"; return false; }
        if( command == "terrain-recovery-source-sample" )
        {
            TerrainData::Heightfield field; uint64_t guid; std::string text;
            return TerrainData::LoadSource( Path( document->GetPath() ).FullPath, field, guid, text, error ) && Sample( field, args, error );
        }
        if( command == "terrain-recovery-reject-dirty" )
        {
            if( !document->IsDirty() || widget->RestoreAssetRecovery( document->GetPath() ) || !document->IsDirty() ) { error = "Restoring over dirty authoring data was not rejected"; return false; }
            return true;
        }
        if( command == "terrain-recovery-external-edit" )
        {
            TerrainData::Heightfield field; uint64_t guid; std::string text, changed;
            return TerrainData::LoadSource( Path( document->GetPath() ).FullPath, field, guid, text, error ) &&
                field.SetSample( { 0, 0 }, std::stof( args ), error ) && TerrainData::SaveSource( Path( document->GetPath() ).FullPath, field, guid, &text, changed, error );
        }
        if( command == "terrain-recovery-external-identity" )
        {
            const auto path = Path( document->GetPath() ).FullPath + ".meta";
            json metadata; { std::ifstream input( path ); input >> metadata; }
            metadata["GUID"] = "0000000000000042";
            std::ofstream output( path ); output << metadata.dump( 4 ); return static_cast<bool>( output );
        }
        if( command == "terrain-recovery-expect-conflict" )
        {
            if( document->Save() || !document->IsDirty() || document->GetError().find( "changed" ) == std::string::npos ) { error = "Recovery overwrote conflicting source data or lost its dirty state"; return false; }
            return true;
        }
        if( command == "terrain-recovery-copy" )
        {
            const auto path = Root() / "RecoveredCopy.terrain";
            if( !document->SaveCopy( path.generic_string() ) ) { error = document->GetError(); return false; }
            TerrainData::Heightfield field; uint64_t guid; std::string text;
            if( !TerrainData::LoadSource( path, field, guid, text, error ) || guid == document->GetGUID() ) return false;
            for( const auto& [coord, tile] : document->GetField().GetTiles() )
                if( !field.FindTile( coord ) || tile.Heights != field.FindTile( coord )->Heights ) { error = "Recovered copy lost unsaved sample data"; return false; }
            return true;
        }
        if( command == "terrain-recovery-defer-stroke" )
        {
            float before; document->GetField().GetSample( { 0, 0 }, before );
            TerrainData::BrushSettings brush; brush.Radius = 2; brush.Strength = 10;
            if( !document->BeginStroke( brush, 0, 0 ) ) return false;
            const bool rejected = !widget->WriteRecoveries(); document->EndStroke( true );
            if( !rejected ) { error = "Recovery accepted a partial stroke"; return false; }
            for( const auto& file : std::filesystem::directory_iterator( Root() / "Snapshots" ) )
            {
                TerrainData::RecoveryInfo info; TerrainData::Heightfield field;
                if( !TerrainData::ReadRecovery( file.path(), info, field, error ) ) return false;
                if( info.AssetPath != document->GetPath() ) continue;
                float actual; field.GetSample( { 0, 0 }, actual );
                if( actual != before ) { error = "Partial stroke replaced the last completed snapshot"; return false; }
                return true;
            }
            error = "Completed snapshot was lost"; return false;
        }
        error = "Unknown terrain recovery command"; return false;
    }
}
#endif
