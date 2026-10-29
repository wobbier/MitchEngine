#include "TerrainPaintAutomation.h"
#if USING( ME_EDITOR )
#include "TerrainWidget.h"
#include "Utils/GUID.h"
#include "Path.h"
#include <sstream>

namespace TerrainPaintAutomation
{
    namespace
    {
        std::shared_ptr<const TerrainData::Surface> s_mark;
        std::shared_ptr<const TerrainData::ResidentTerrain> s_geometry;
        bool s_marked = false;
        std::filesystem::path s_recovery;
    }


    bool Execute( const std::string& command, const std::string& args, std::string& error )
    {
        auto* widget = TerrainWidget::Get(); auto* document = widget ? widget->GetActive() : nullptr;
        if( !document ) { error = "Open a terrain source first"; return false; }
        if( command == "terrain-paint-layer" )
        {
            const int layer = std::stoi( args ); if( layer < 0 || layer >= 4 ) return false;
            widget->Brush.Layer = static_cast<unsigned>( layer ); return true;
        }
        if( command == "terrain-paint-fill" ) return widget->FillLayer( static_cast<unsigned>( std::stoul( args ) ) );
        if( command == "terrain-paint-disable" ) return document->SetSurface( nullptr );
        if( command == "terrain-paint-palette" )
        {
            const auto value = json::parse( args ); const unsigned index = value.at( "Layer" ).get<unsigned>(); if( index >= 4 ) return false;
            auto surface = document->GetField().GetSurface() ? std::make_shared<TerrainData::Surface>( *document->GetField().GetSurface() ) : std::make_shared<TerrainData::Surface>();
            auto& layer = surface->Layers[index]; layer.Name = value.at( "Name" ).get<std::string>();
            layer.Color = value.at( "Color" ).get<std::array<float, 3>>(); layer.Roughness = value.at( "Roughness" ).get<float>();
            return document->SetSurface( surface );
        }
        if( command == "terrain-paint-mark" )
        { if( !document->WaitForPreview() ) return false; s_mark = document->GetField().GetSurface(); s_geometry = document->GetPreview(); s_marked = true; return true; }
        if( command == "terrain-paint-assert-mark" )
        {
            const auto source = document->GetField().GetSurface();
            if( !s_marked || bool( source ) != bool( s_mark ) || ( source && *source != *s_mark ) )
            { error = "Terrain surface differs from the marked weights/palette"; return false; }
            return true;
        }
        if( command == "terrain-paint-assert-geometry" )
        {
            if( !document->WaitForPreview() ) return false;
            const auto& current = document->GetPreview();
            if( !s_geometry || current->Patches != s_geometry->Patches || current->CollisionTiles != s_geometry->CollisionTiles )
            { error = "Surface editing rebuilt terrain geometry or collision"; return false; }
            return true;
        }
        if( command == "terrain-paint-assert-weights" )
        {
            std::istringstream stream( args ); int64_t x, z; std::array<int, 4> expected;
            if( !( stream >> x >> z >> expected[0] >> expected[1] >> expected[2] >> expected[3] ) ) return false;
            const auto source = document->GetField().GetSurface();
            const auto weights = source ? source->Get( { x, z } ) : TerrainData::LayerWeights{ 255, 0, 0, 0 };
            for( size_t i = 0; i < 4; ++i ) if( weights[i] != expected[i] ) { error = "Terrain paint weights differ at the requested sample"; return false; }
            return true;
        }
        if( command == "terrain-paint-recovery-save" )
        {
            s_recovery = std::filesystem::path( Path( ".tmp/Tests/TerrainPaintRecovery/" + std::to_string( EngineGUID::Generate() ) ).FullPath );
            if( !document->SaveRecovery( s_recovery / "Owned.terrain-recovery" ) ) return false;
            // Keep a crash-surviving copy while Load clears this live document's own snapshot.
            std::filesystem::copy_file( s_recovery / "Owned.terrain-recovery", s_recovery / "Restore.terrain-recovery" );
            return true;
        }
        if( command == "terrain-paint-recovery-restore" ) return document->RestoreRecovery( s_recovery / "Restore.terrain-recovery" );
        error = "Unknown terrain paint automation command"; return false;
    }
}
#endif
