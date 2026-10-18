#include "TerrainLodAutomation.h"
#if USING( ME_EDITOR )
#include "TerrainWidget.h"
#include "Components/Graphics/Terrain.h"
#include "Components/Camera.h"
#include "Components/Transform.h"
#include "Cores/EditorCore.h"
#include "Cores/PhysicsCore.h"
#include "Editor/EditorOperations.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EditorApp.h"
#include "Havana.h"
#include "Renderer.h"
#include "Widgets/SceneViewWidget.h"
#include "World/Scene.h"
#include "Utils/GUID.h"
#include "Path.h"
#include <SDL.h>
#include <imgui.h>
#include <cmath>
#include <algorithm>
#include <fstream>
#include <sstream>

namespace TerrainLodAutomation
{
    namespace
    {
        std::filesystem::path s_directory;
        std::shared_ptr<const TerrainData::ResidentTerrain> s_source;
        uint64_t s_collisionBuilds = 0, s_triangles = 0;
        std::vector<uint16_t> s_vertices;
        std::vector<TerrainData::PatchLodChoice> s_choices;
        EntityHandle Ground() { return EditorOps::GetWorld().FindEntityByName( "Terrain" ); }
        void CameraPose( bool far )
        {
            const Vector3 position = far ? Vector3( 0, 80, -400 ) : Vector3( 0, 18, -80 );
            auto* app = static_cast<EditorApp*>( GetEngine().GetGame() );
            app->EditorSceneManager->GetCameraController().SetPose( position, 10.f, 0.f );
            if( auto main = EditorOps::GetWorld().FindEntityByName( "Main Camera" ) )
            {
                main->GetComponent<Transform>().SetPosition( position ); main->GetComponent<Transform>().SetRotation( Vector3( 10, 0, 0 ) );
                main->GetComponent<Camera>().Far = 2000;
            }
        }
        std::vector<uint16_t> VertexBuffers( Entity& entity )
        {
            std::vector<std::pair<uint32_t, uint16_t>> buffers;
            for( const auto& command : GetEngine().GetRenderer().GetMeshCache().Commands )
                if( command.SingleMesh && command.ID == entity.GetId().Value() ) buffers.emplace_back( command.VisibilityIndex, command.VertexBufferIdx );
            std::sort( buffers.begin(), buffers.end() );
            std::vector<uint16_t> result; for( const auto& pair : buffers ) result.push_back( pair.second ); return result;
        }
    }


    bool Execute( const std::string& command, const std::string& args, std::string& error )
    {
        auto* widget = TerrainWidget::Get();
        if( command == "terrain-lod-fixture" )
        {
            s_directory = std::filesystem::path( Path( ".tmp/Tests/TerrainLodFlows" ).FullPath ) / std::to_string( GUID::Generate() );
            TerrainData::Settings s; s.TileQuads = 64; s.PatchQuads = 8; s.MinTile = { -1, -1 }; s.TilesX = s.TilesZ = 2;
            TerrainData::Heightfield field( s );
            for( int x = -1; x <= 0; ++x ) for( int z = -1; z <= 0; ++z ) if( !field.CreateFlatTile( { x, z }, 0, error ) ) return false;
            for( int z = -64; z <= 64; ++z ) for( int x = -64; x <= 64; ++x )
                if( !field.SetSample( { x, z }, static_cast<float>( 3 * std::sin( x * 0.19 ) * std::cos( z * 0.17 ) + 1.2 * std::sin( ( x + z ) * 0.07 ) ), error ) ) return false;
            if( !widget->CreateFromField( ( s_directory / "Source.terrain" ).generic_string(), std::move( field ) ) ) return false;
            CameraPose( false ); return true;
        }
        auto entity = Ground(); auto* terrain = entity ? entity->TryGetComponent<Terrain>() : nullptr;
        if( !terrain || !terrain->GetData() ) { error = "Wait for the LOD terrain instance"; return false; }
        if( command == "terrain-lod-pointer" )
        {
            std::istringstream stream( args ); std::string target, state;
            if( !( stream >> target >> state ) || ( target != "Patches" && target != "LODs" ) ) return false;
            const auto center = widget->GetLodButtonCenter( target == "Patches" );
            if( center.IsZero() ) { error = "Inspect LOD buttons were not drawn"; return false; }
            SDL_WarpMouseGlobal( static_cast<int>( center.x ), static_cast<int>( center.y ) );
            ImGui::GetIO().AddMousePosEvent( center.x, center.y );
            if( state == "down" || state == "up" ) ImGui::GetIO().AddMouseButtonEvent( 0, state == "down" );
            else if( state != "hover" ) return false;
            return true;
        }
        if( command == "terrain-lod-assert-debug" )
        {
            if( !terrain->ShowLods || !terrain->ShowPatches ) { error = "Inspect buttons did not enable the LOD debug views"; return false; }
            return true;
        }
        if( command == "terrain-lod-camera" ) { CameraPose( args == "far" ); return true; }
        if( command == "terrain-lod-force" ) { terrain->ForcedLod = std::stoi( args ); return true; }
        if( command == "terrain-lod-debug" ) { terrain->ShowLods = terrain->ShowPatches = args == "true"; return true; }
        if( command == "terrain-lod-wire" )
        {
            auto* app = static_cast<EditorApp*>( GetEngine().GetGame() );
            app->Editor->GetSceneView()->SetShadingMode( args == "true" ? Moonlight::ViewShadingMode::Wireframe : Moonlight::ViewShadingMode::Lit ); return true;
        }
        if( command == "terrain-lod-aux" )
        {
            auto existing = EditorOps::GetWorld().FindEntityByName( "Terrain Detail Camera" );
            if( args == "false" ) { if( existing ) existing->MarkForDelete(); return true; }
            if( existing ) return false;
            auto camera = EditorOps::GetWorld().CreateEntity( "Terrain Detail Camera" );
            camera->AddComponent<Transform>().SetPosition( Vector3( 0, 0, -200 ) );
            auto& settings = camera->AddComponent<Camera>(); settings.Projection = Moonlight::ProjectionType::Orthographic;
            settings.OutputSize = Vector2( 256, 256 ); settings.OrthographicSize = 0.05f; settings.Far = 1000;
            return true;
        }
        const auto& choices = terrain->GetPatchLods();
        if( choices.empty() || !terrain->GetLodError().empty() ) { error = "Terrain LOD selection failed: " + terrain->GetLodError(); return false; }
        if( command == "terrain-lod-mark" )
        {
            s_source = terrain->GetData(); s_collisionBuilds = GetEngine().Physics->GetTerrainCollisionBuildCount();
            s_vertices = VertexBuffers( *entity.Get() ); s_choices = choices; s_triangles = terrain->GetLodTriangles(); return true;
        }
        if( command == "terrain-lod-assert-mark" )
        {
            if( terrain->GetData() != s_source || GetEngine().Physics->GetTerrainCollisionBuildCount() != s_collisionBuilds || VertexBuffers( *entity.Get() ) != s_vertices )
            { error = "LOD changes rebuilt canonical data, collision cooks or vertex buffers"; return false; }
            return true;
        }
        if( command == "terrain-lod-assert-level" )
        {
            for( const auto& choice : choices ) if( choice.Level != std::stoi( args ) || choice.Edges ) { error = "Forced terrain level did not select uniform topology"; return false; }
            return true;
        }
        if( command == "terrain-lod-assert-union" )
        {
            if( choices.size() != s_choices.size() || terrain->GetLodTriangles() <= s_triangles ) { error = "Auxiliary camera did not request finer terrain detail"; return false; }
            for( size_t i = 0; i < choices.size(); ++i ) if( choices[i].Level > s_choices[i].Level ) { error = "Auxiliary camera lost existing terrain detail"; return false; }
            return true;
        }
        if( command == "terrain-lod-assert-mixed" )
        {
            bool mixed = false, stitched = false;
            for( const auto& choice : choices )
            {
                mixed |= choice.Level != choices.front().Level; stitched |= choice.Edges != 0;
                if( choice.PixelError > terrain->LodPixelError + 1e-5 ) { error = "Terrain transition exceeded its pixel budget"; return false; }
            }
            if( !mixed || !stitched ) { error = "Expected mixed, stitched terrain levels"; return false; }
        }
        else if( command == "terrain-lod-assert-reduced" )
        {
            if( terrain->GetLodTriangles() >= terrain->GetFullDetailTriangles() ) { error = "LOD did not reduce terrain triangles"; return false; }
        }
        else if( command == "terrain-lod-collision" )
        {
            const auto expected = terrain->GetData()->Heights->Query( -11.25, 7.6 ); RaycastHit hit;
            if( !GetEngine().Physics->Raycast( Vector3( -11.25f, 30, 7.6f ), Vector3( 0, -1, 0 ), 100, hit ) || hit.Entity != entity || std::abs( hit.Position.y - expected.Height ) > 1e-4 )
            { error = "Terrain collision changed with visual LOD"; return false; }
        }
        else if( command == "terrain-lod-serialize" )
        {
            const auto data = EditorOps::CaptureComponent( *entity.Get(), "Terrain" );
            if( !data.contains( "LodPixelError" ) || data.contains( "ForcedLod" ) || data.contains( "ShowLods" ) || data.contains( "ShowPatches" ) )
            { error = "Terrain LOD settings/debug serialization is incorrect"; return false; }
            GetEngine().CurrentScene->Save( ( s_directory / "Runtime.lvl" ).generic_string(), EditorOps::GetSceneRoot() );
            std::ofstream( s_directory.parent_path() / "LatestScene.txt" ) << ( s_directory / "Runtime.lvl" ).generic_string();
        }
        else if( command != "terrain-lod-render" ) { error = "Unknown terrain LOD command"; return false; }
        uint64_t triangles = 0; size_t patches = 0;
        for( const auto& mesh : GetEngine().GetRenderer().GetMeshCache().Commands ) if( mesh.SingleMesh && mesh.ID == entity->GetId().Value() )
        {
            if( mesh.IndexBufferIdx != mesh.SingleMesh->GetIndexBuffer( mesh.Lod ).idx || !bgfx::isValid( mesh.SingleMesh->GetIndexBuffer( mesh.Lod ) ) )
            { error = "Terrain render commands use a different topology"; return false; }
            triangles += mesh.SingleMesh->GetIndexCount( mesh.Lod ) / 3; ++patches;
        }
        if( patches != choices.size() || triangles != terrain->GetLodTriangles() ) { error = "Terrain LOD draw/stat counts disagree"; return false; }
        return true;
    }
}
#endif
