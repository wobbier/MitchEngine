#include "TerrainHardeningAutomation.h"
#if USING( ME_EDITOR )
#include "TerrainWidget.h"
#include "Terrain/TerrainAsset.h"
#include "Utils/GUID.h"
#include "CLog.h"
#include "Path.h"
#include <chrono>
#include <fstream>

namespace TerrainHardeningAutomation
{
    namespace
    {
        std::shared_ptr<const TerrainData::ResidentTerrain> s_idle;
        uint64_t s_started = 0;


        bool Equal( const TerrainData::Heightfield& a, const TerrainData::Heightfield& b )
        {
            if( a.GetTiles().size() != b.GetTiles().size() ) return false;
            for( const auto& [coord, tile] : a.GetTiles() ) if( !b.FindTile( coord ) || b.FindTile( coord )->Heights != tile.Heights ) return false;
            return bool( a.GetSurface() ) == bool( b.GetSurface() ) && ( !a.GetSurface() || *a.GetSurface() == *b.GetSurface() );
        }
    }


    bool Execute( const std::string& command, const std::string& args, std::string& error )
    {
        auto* widget = TerrainWidget::Get(); auto* document = widget ? widget->GetActive() : nullptr;
        if( !document ) { error = "Open a terrain document first"; return false; }
        if( command == "terrain-harden-hide" ) { widget->IsOpen = false; return true; }
        if( command == "terrain-harden-ready" || command == "terrain-harden-idle-mark" || command == "terrain-harden-idle-assert" )
        {
            const auto& preview = document->GetPreview();
            if( document->IsPreparingPreview() || !preview || !Equal( document->GetField(), *preview->Heights ) || !document->GetPreviewError().empty() )
            { error = "Latest canonical authoring data did not publish in the background"; return false; }
            if( command == "terrain-harden-idle-mark" ) { s_idle = preview; s_started = document->GetPreviewStatistics().Started; }
            if( command == "terrain-harden-idle-assert" && ( s_idle != preview || s_started != document->GetPreviewStatistics().Started ) )
            { error = "An idle document restarted preview preparation"; return false; }
            return true;
        }
        if( document->IsDirty() ) { error = "Start this hardening check from a saved terrain"; return false; }
        if( command == "terrain-harden-limits" )
        {
            const auto root = std::filesystem::path( Path( ".tmp/Tests/TerrainHardening/" + std::to_string( Guid::Generate() ) ).FullPath );
            std::filesystem::create_directories( root );
            TerrainData::Manifest manifest; manifest.AssetGUID = document->GetGUID();
            manifest.Config.TileQuads = 32; manifest.Config.PatchQuads = 16; manifest.Config.TilesX = manifest.Config.TilesZ = 32;
            for( int x = 0; x < 32; ++x ) for( int z = 0; z < 32; ++z ) manifest.Pages.push_back( { { x, z }, "Missing/" + std::to_string( x ) + "_" + std::to_string( z ) + ".thp" } );
            std::string text; if( !TerrainData::SerializeManifest( manifest, text, error ) ) return false;
            const auto path = root / "Oversized.terrain"; { std::ofstream file( path ); file << text; }
            TerrainDocument oversized( path.generic_string() );
            if( oversized.Load() || oversized.GetError().find( "sample budget" ) == std::string::npos )
            { error = "Oversized source was not rejected before page reads"; return false; }
            TerrainData::Heightfield field( manifest.Config );
            for( int x = 0; x < 32; ++x ) for( int z = 0; z < 32; ++z ) if( !field.CreateFlatTile( { x, z }, 0, error ) ) return false;
            if( oversized.CreateFromField( field ) || oversized.GetError().find( "one million" ) == std::string::npos )
            { error = "Oversized source creation escaped the authoring limit"; return false; }
            TerrainData::RecoveryInfo info; info.AssetPath = document->GetPath(); info.AssetGUID = document->GetGUID(); info.SavedAt = 1; info.BaseManifest = text;
            const auto recovery = root / "Oversized.terrain-recovery";
            if( !TerrainData::WriteRecovery( recovery, info, field, error ) ) return false;
            const auto preview = document->GetPreview(); const auto revision = document->GetField().GetRevision();
            if( document->RestoreRecovery( recovery ) || document->GetError().find( "one million" ) == std::string::npos ||
                document->GetPreview() != preview || document->GetField().GetRevision() != revision || document->IsDirty() )
            { error = "Rejected oversized recovery changed the current document"; return false; }
            return document->Save();
        }
        if( command != "terrain-harden-cycle" ) { error = "Unknown terrain hardening command"; return false; }
        const unsigned count = args.empty() ? 32 : static_cast<unsigned>( std::stoul( args ) );
        if( !count || count > 256 ) { error = "Hardening cycles must be between 1 and 256"; return false; }
        const auto before = document->GetField(); const auto stats = document->GetPreviewStatistics(); const auto started = std::chrono::steady_clock::now();
        for( unsigned i = 0; i < count; ++i )
        {
            TerrainData::BrushSettings brush; brush.Radius = 3; brush.Strength = 0.125f;
            if( !document->BeginStroke( brush, -3, -3 ) || !document->MoveStroke( 3, 3 ) ) { error = document->GetError(); return false; }
            document->EndStroke();
            if( i % 3 == 0 )
            {
                const auto capture = document->GetField(); brush.Tool = TerrainData::BrushTool::Lower;
                if( !document->BeginStroke( brush, -3, 3 ) || !document->MoveStroke( 3, -3 ) ) return false;
                document->EndStroke( true ); if( !Equal( capture, document->GetField() ) ) { error = "Cancelled stroke changed canonical data"; return false; }
            }
            auto& history = document->GetUndo(); history.Undo(); history.Redo(); history.Undo();
            if( !Equal( before, document->GetField() ) ) { error = "Sculpt undo drifted during repeated edits"; return false; }
            auto surface = before.GetSurface() ? std::make_shared<TerrainData::Surface>( *before.GetSurface() ) : std::make_shared<TerrainData::Surface>();
            surface->Set( { 0, 0 }, { 0, 255, 0, 0 } ); if( !document->SetSurface( surface ) ) return false;
            history.Undo(); history.Redo(); history.Undo();
            if( !Equal( before, document->GetField() ) || document->IsDirty() ) { error = "Paint/history save point drifted during repeated edits"; return false; }
            if( i % 8 == 0 && !document->Save() ) { error = document->GetError(); return false; }
            if( i % 16 == 0 && !document->Load() ) { error = document->GetError(); return false; }
        }
        if( !document->WaitForPreview() || !Equal( before, *document->GetPreview()->Heights ) ) { error = document->GetError(); return false; }
        const auto& after = document->GetPreviewStatistics();
        CLog::GetInstance().Log( CLog::LogType::Info, "[terrain-hardening] cycles=" + std::to_string( count ) + " builds=" + std::to_string( after.Started - stats.Started ) +
            " discarded=" + std::to_string( after.Discarded - stats.Discarded ) + " milliseconds=" + std::to_string( std::chrono::duration<double, std::milli>( std::chrono::steady_clock::now() - started ).count() ) );
        return true;
    }
}
#endif
