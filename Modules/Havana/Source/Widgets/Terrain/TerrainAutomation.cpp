#include "TerrainAutomation.h"
#if USING( ME_EDITOR )
#include "TerrainWidget.h"
#include "Editor/EditorOperations.h"
#include "Editor/Selection.h"
#include "Components/Graphics/Terrain.h"
#include "Components/Transform.h"
#include "Engine/World.h"
#include "Utils/GUID.h"
#include "Widgets/SceneViewWidget.h"
#include "Editor/SceneTools.h"
#include "EditorApp.h"
#include "Engine/Engine.h"
#include "Havana.h"
#include "imgui_internal.h"
#include "ImGuiTheme.h"
#include <SDL.h>
#include <sstream>
#include <cmath>

namespace TerrainAutomation
{
    namespace
    {
        std::string s_fixture;
        std::unique_ptr<TerrainData::Heightfield> s_mark;
        std::string s_rawFile;
        TerrainData::Settings s_rawSettings;
        TerrainData::RawHeightmap s_rawFormat;
    }
    bool Execute( const std::string& command, const std::string& args, std::string& error )
    {
        auto* widget = TerrainWidget::Get();
        if( !widget ) { error = "Terrain panel is unavailable"; return false; }
        auto* document = widget->GetActive();
        if( command == "terrain-fixture" )
        {
            s_fixture = ".tmp/Tests/TerrainAuthoringFlows/" + std::to_string( GUID::Generate() ) + "/Source.terrain";
            TerrainData::Settings settings; settings.TileQuads = 32; settings.PatchQuads = 16;
            settings.MinTile = { -1, -1 }; settings.TilesX = settings.TilesZ = 2;
            return widget->Create( s_fixture, settings ) != nullptr;
        }
        if( command == "terrain-open" ) return widget->Open( args ) != nullptr;
        if( !document ) { error = "Open a terrain first"; return false; }
        if( command == "terrain-stroke" )
        {
            const auto value = json::parse( args, nullptr, false );
            if( value.is_discarded() || !value.is_object() || !value.contains( "From" ) || !value.contains( "To" ) ) { error = "Expected stroke JSON with From and To [x,z]"; return false; }
            TerrainData::BrushSettings brush;
            const std::string tool = value.value( "Tool", std::string( "Raise" ) );
            if( tool == "Lower" ) brush.Tool = TerrainData::BrushTool::Lower;
            else if( tool == "Smooth" ) brush.Tool = TerrainData::BrushTool::Smooth;
            else if( tool == "Flatten" ) brush.Tool = TerrainData::BrushTool::Flatten;
            else if( tool != "Raise" ) { error = "Unknown brush tool"; return false; }
            brush.Radius = value.value( "Radius", 4.f ); brush.Strength = value.value( "Strength", 0.5f );
            brush.Falloff = value.value( "Falloff", 0.65f ); brush.Spacing = value.value( "Spacing", 0.2f );
            if( !document->BeginStroke( brush, value["From"][0].get<double>(), value["From"][1].get<double>() ) ||
                !document->MoveStroke( value["To"][0].get<double>(), value["To"][1].get<double>() ) )
            { error = document->GetError(); document->EndStroke( true ); return false; }
            document->EndStroke( value.value( "Cancel", false ) );
        }
        else if( command == "terrain-save" ) { if( !document->Save() ) { error = document->GetError(); return false; } }
        else if( command == "terrain-reload" ) { if( !document->Load() ) { error = document->GetError(); return false; } }
        else if( command == "terrain-mark" ) s_mark = std::make_unique<TerrainData::Heightfield>( document->GetField() );
        else if( command == "terrain-assert-mark" )
        {
            if( !s_mark ) { error = "Use terrain-mark first"; return false; }
            for( const auto& [coordinate, tile] : s_mark->GetTiles() )
            {
                const auto* other = document->GetField().FindTile( coordinate );
                if( !other || tile.Heights != other->Heights ) { error = "Terrain samples differ from the mark"; return false; }
            }
        }
        else if( command == "terrain-assert-sample" )
        {
            std::istringstream stream( args ); int64_t x, z; float expected, height;
            if( !( stream >> x >> z >> expected ) || document->GetField().GetSample( { x, z }, height ) != TerrainData::QueryStatus::Ready || std::abs( height - expected ) > 0.0001f )
            { error = "Terrain sample differs from expected height"; return false; }
        }
        else if( command == "terrain-assert-dirty" )
        {
            if( document->IsDirty() != ( args == "true" ) ) { error = "Unexpected terrain document dirty state"; return false; }
        }
        else if( command == "terrain-bind" )
        {
            auto entity = EditorOps::GetWorld().FindEntityByName( args );
            if( !entity || !entity->HasComponent<Terrain>() ) { error = "Expected an entity with Terrain"; return false; }
            const auto before = EditorOps::CaptureComponent( *entity.Get(), "Terrain" );
            entity->GetComponent<Terrain>().Asset = Path( document->GetPath() );
            EditorOps::RecordComponentEdit( *entity.Get(), "Terrain", before, EditorOps::CaptureComponent( *entity.Get(), "Terrain" ), "Bind terrain fixture" );
            Selection::Get().Set( entity );
        }
        else if( command == "terrain-assert-preview" )
        {
            auto entity = EditorOps::GetWorld().FindEntityByName( args );
            auto* terrain = entity ? entity->TryGetComponent<Terrain>() : nullptr;
            if( !terrain || terrain->GetData() != document->GetPreview() ) { error = "Scene instance does not use the shared authoring snapshot"; return false; }
        }
        else if( command == "terrain-mode" )
        {
            const char* modes[] = { "Sculpt", "Paint", "Tiles", "Foliage", "Splines", "Inspect" };
            for( int i = 0; i < 6; ++i ) if( args == modes[i] ) { widget->SetCategory( i ); return true; }
            error = "Unknown terrain workspace"; return false;
        }
        else if( command == "terrain-brush" ) widget->BrushEnabled = args == "true";
        else if( command == "terrain-layout" )
        {
            if( auto* window = ImGui::FindWindowByName( "Terrain" ) )
            {
                const auto* viewport = ImGui::GetMainViewport();
                ImGui::SetWindowDock( window, 0, ImGuiCond_Always );
                ImGui::SetWindowPos( window, ImVec2( viewport->WorkPos.x + ImGuiTheme::Em( 3 ), viewport->WorkPos.y + ImGuiTheme::Em( 3 ) ), ImGuiCond_Always );
                ImGui::SetWindowSize( window, ImVec2( ImGuiTheme::Em( 25 ), std::min( ImGuiTheme::Em( 52 ), viewport->WorkSize.y - ImGuiTheme::Em( 4 ) ) ), ImGuiCond_Always );
            }
        }
        else if( command == "terrain-escape" ) ImGui::GetIO().AddKeyEvent( ImGuiKey_Escape, args == "true" );
        else if( command == "terrain-export-fixture" )
        {
            s_rawSettings = document->GetField().GetSettings();
            s_rawFormat.Width = s_rawSettings.TilesX * s_rawSettings.TileQuads + 1;
            s_rawFormat.Height = s_rawSettings.TilesZ * s_rawSettings.TileQuads + 1;
            s_rawFormat.Encoding = TerrainData::HeightEncoding::Float32;
            s_rawFormat.BigEndian = true; s_rawFormat.FlipZ = true; s_rawFormat.Offset = 0; s_rawFormat.Scale = 1;
            s_rawFile = ".tmp/Tests/TerrainHeightmapFlows/" + std::to_string( GUID::Generate() ) + "/Height.r32";
            return TerrainData::WriteHeightmap( Path( s_rawFile ).FullPath, document->GetField(), s_rawFormat, error );
        }
        else if( command == "terrain-import-fixture" )
        {
            TerrainData::Heightfield imported;
            if( s_rawFile.empty() || !TerrainData::ReadHeightmap( Path( s_rawFile ).FullPath, s_rawSettings, s_rawFormat, imported, error ) ) return false;
            return widget->CreateFromField( std::filesystem::path( s_rawFile ).replace_filename( "Imported.terrain" ).generic_string(), std::move( imported ) ) != nullptr;
        }
        else if( command == "terrain-unique" )
        {
            const std::string path = ".tmp/Tests/TerrainHeightmapFlows/" + std::to_string( GUID::Generate() ) + "/Unique.terrain";
            return widget->MakeUnique( path );
        }
        else if( command == "terrain-assert-independent" || command == "terrain-assert-marked-instance" )
        {
            auto entity = EditorOps::GetWorld().FindEntityByName( args );
            const auto* terrain = entity ? entity->TryGetComponent<Terrain>() : nullptr;
            const auto data = terrain ? terrain->GetData() : nullptr;
            if( !data || data->AssetGUID == document->GetGUID() ) { error = "Expected an independent terrain asset"; return false; }
            if( command == "terrain-assert-marked-instance" )
            {
                if( !s_mark ) { error = "Use terrain-mark first"; return false; }
                for( const auto& [coordinate, tile] : s_mark->GetTiles() )
                {
                    const auto* other = data->Heights->FindTile( coordinate );
                    if( !other || other->Heights != tile.Heights ) { error = "Shared source instance changed after making another instance unique"; return false; }
                }
            }
        }
        else if( command == "terrain-pointer" )
        {
            // Real ImGui input on the private headless desktop. This uses FreeForm World View.
            std::istringstream stream( args ); float x = 0, z = 0; std::string button;
            if( !( stream >> x >> z >> button ) ) { error = "Expected terrain-pointer x z hover|down|up"; return false; }
            auto* app = static_cast<EditorApp*>( GetEngine().GetGame() );
            auto* view = app->Editor->GetSceneView();
            auto* window = ImGui::FindWindowByName( "World View" );
            auto entity = Selection::Get().GetActive();
            const auto height = document->GetField().Query( x, z );
            if( !stream || !view || !view->MainCamera || !window || !entity || height.Status != TerrainData::QueryStatus::Ready )
            { error = "Expected terrain-pointer x z hover|down|up with a selected terrain and visible World View"; return false; }
            const Vector3 point = Vector3( x, height.Height, z ) + entity->GetComponent<Transform>().GetWorldPosition();
            Vector2 pixel;
            const Matrix4 vp( view->MainCamera->ProjectionMatrix.GetInternalMatrix() * view->MainCamera->View.GetInternalMatrix() );
            if( !SceneTools::WorldToScreen( point, view->SceneViewRenderSize, vp, pixel ) ) { error = "Pointer target is behind the camera"; return false; }
            const ImVec2 screen( window->Pos.x + view->SceneViewRenderLocation.x + pixel.x, window->Pos.y + view->SceneViewRenderLocation.y + pixel.y );
            SDL_WarpMouseGlobal( static_cast<int>( screen.x ), static_cast<int>( screen.y ) );
            ImGui::GetIO().AddMousePosEvent( screen.x, screen.y );
            if( button == "down" || button == "up" ) ImGui::GetIO().AddMouseButtonEvent( ImGuiMouseButton_Left, button == "down" );
            else if( button != "hover" ) { error = "Expected hover, down or up"; return false; }
        }
        else { error = "Unknown terrain automation command"; return false; }
        return true;
    }
}
#endif
