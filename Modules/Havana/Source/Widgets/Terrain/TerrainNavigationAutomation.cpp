#include "TerrainNavigationAutomation.h"
#if USING( ME_EDITOR )
#include "TerrainWidget.h"
#include "Components/Graphics/Terrain.h"
#include "Components/Graphics/Mesh.h"
#include "Components/Navigation/NavMeshSurface.h"
#include "Components/Navigation/NavMeshAgent.h"
#include "Components/Camera.h"
#include "Components/Transform.h"
#include "Cores/NavigationCore.h"
#include "Cores/TerrainCore.h"
#include "Editor/EditorOperations.h"
#include "Editor/Selection.h"
#include "EditorApp.h"
#include "Cores/EditorCore.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Terrain/TerrainSurface.h"
#include "Utils/GUID.h"
#include <cmath>
#include <filesystem>

namespace TerrainNavigationAutomation
{
    namespace
    {
        std::filesystem::path s_directory;
        uint64_t s_hash = 0;
        bool s_streamed = false;
        EntityHandle Ground() { return EditorOps::GetWorld().FindEntityByName( "Terrain" ); }


        EntityHandle Owner() { return EditorOps::GetWorld().FindEntityByName( "Terrain Nav" ); }


        EntityHandle Walker() { return EditorOps::GetWorld().FindEntityByName( "Terrain Walker" ); }


        void Pose( bool away )
        {
            const Vector3 eye = away ? Vector3( 120, 28, 120 ) : Vector3( 7, 28, -37 );
            static_cast<EditorApp*>( GetEngine().GetGame() )->EditorSceneManager->GetCameraController().SetPose( eye, 40, 0 );
            if( auto camera = EditorOps::GetWorld().FindEntityByName( "Main Camera" ) )
            { camera->GetComponent<Transform>().SetPosition( eye ); camera->GetComponent<Transform>().SetRotation( { 40, 0, 0 } ); }
        }
    }


    bool Execute( const std::string& command, const std::string& args, std::string& error )
    {
        auto* widget = TerrainWidget::Get(); auto& world = EditorOps::GetWorld(); auto* nav = world.GetCore<NavigationCore>();
        if( !widget || !nav ) return false;
        if( command == "terrain-nav-fixture" )
        {
            s_streamed = false; s_directory = std::filesystem::path( Path( ".tmp/Tests/TerrainNavigationFlows" ).FullPath ) / std::to_string( GUID::Generate() );
            TerrainData::Settings s; s.TileQuads = 16; s.PatchQuads = 8; s.MinTile = { -2, -2 }; s.TilesX = s.TilesZ = 4;
            TerrainData::Heightfield field( s );
            for( int x = -2; x < 2; ++x ) for( int z = -2; z < 2; ++z ) if( !field.CreateFlatTile( { x, z }, 0, error ) ) return false;
            for( int z = -32; z <= 32; ++z ) for( int x = -32; x <= 32; ++x ) if( !field.SetSample( { x, z }, x * 0.025f, error ) ) return false;
            if( !field.SetSurface( std::make_shared<TerrainData::Surface>(), error ) || !widget->CreateFromField( ( s_directory / "Source.terrain" ).generic_string(), std::move( field ) ) ) return false;
            auto& terrain = Ground()->GetComponent<Terrain>(); terrain.Asset = Path( ( s_directory / "Source.terrain" ).generic_string() ); terrain.ReloadAsset();
            for( int i = 1; i <= 3; ++i ) if( auto box = world.FindEntityByName( "Terrain Box " + std::to_string( i ) ) ) box->MarkForDelete();
            auto owner = world.CreateEntity( "Terrain Nav" ); owner->AddComponent<Transform>().SetPosition( { 7, 0, -5 } );
            auto& surface = owner->AddComponent<NavMeshSurface>(); surface.Collect = NavCollectObjects::Children;
            surface.CellSize = 0.3f; surface.CellHeight = 0.1f; surface.AgentRadius = 0.3f; surface.TileSize = 16; surface.MinRegionSize = 1; surface.MergeRegionSize = 2;
            surface.NavMeshData = ( s_directory / "Resident.navmesh" ).generic_string();
            Ground()->GetComponent<Transform>().SetParent( owner->GetComponent<Transform>() ); Ground()->GetComponent<Transform>().SetPosition( {} );
            auto walker = world.CreateEntity( "Terrain Walker" ); walker->AddComponent<Transform>().SetPosition( { -3, 0, -17 } );
            walker->AddComponent<Mesh>(); auto& agent = walker->AddComponent<NavMeshAgent>(); agent.Speed = 6; agent.Acceleration = 30; agent.Radius = 0.3f;
            NavigationCore::DebugDrawEnabled = true; Pose( false ); Selection::Get().Set( owner ); return true;
        }
        auto owner = Owner(); auto ground = Ground();
        if( !owner || !ground ) { error = "Terrain navigation fixture is unavailable"; return false; }
        auto& surface = owner->GetComponent<NavMeshSurface>(); auto& terrain = ground->GetComponent<Terrain>();
        if( command == "terrain-nav-bake" ) return nav->Bake( surface, true );
        if( command == "terrain-nav-wait" ) { nav->WaitForBakes(); return true; }
        if( command == "terrain-nav-cook" ) return widget->StartStreamCook( ( s_directory / "Runtime.terrainc" ).generic_string(), 2, true );
        if( command == "terrain-nav-cook-wait" ) return widget->FinishStreamCook( true );
        if( command == "terrain-nav-bind-stream" )
        {
            s_streamed = true; terrain.Asset = Path( ( s_directory / "Runtime.terrainc" ).generic_string() ); terrain.ReloadAsset();
            terrain.StreamDetail = false; terrain.CoverageDistance = 8; terrain.StreamDistance = 0;
            surface.Collect = NavCollectObjects::Volume; surface.VolumeSize = { 12, 8, 12 }; surface.NavMeshData = ( s_directory / "Streamed.navmesh" ).generic_string();
            Walker()->GetComponent<Transform>().SetPosition( { 3, 0, -9 } ); Pose( true ); return true;
        }
        if( command == "terrain-nav-camera-home" ) { terrain.CoverageDistance = 48; Pose( false ); return true; }
        if( command == "terrain-nav-auto-reload" )
        {
            surface.NavMeshData = ( s_directory / "Auto.navmesh" ).generic_string(); surface.BakeOnLoad = true; terrain.ReloadAsset(); return true;
        }
        if( command == "terrain-nav-go" )
        {
            const auto origin = ground->GetComponent<Transform>().GetWorldPosition(); const float d = s_streamed ? 4.f : 12.f;
            auto& agent = Walker()->GetComponent<NavMeshAgent>(); agent.Warp( origin + Vector3( -d, 0, -d ) ); agent.SetDestination( origin + Vector3( d, 0, d ) ); return true;
        }
        if( command == "terrain-nav-arrived" )
        {
            const auto& agent = Walker()->GetComponent<NavMeshAgent>();
            if( !agent.HasArrived() || !agent.IsOnNavMesh() ) { error = "Terrain navigation agent did not arrive"; return false; } return true;
        }
        const auto* mesh = nav->GetNavMesh( surface );
        if( !mesh ) { error = "Terrain navigation bake has not loaded: " + nav->GetSurfaceInfo( surface ).LastError; return false; }
        if( command == "terrain-nav-mark" ) { s_hash = mesh->GetData().SourceHash; return true; }
        if( command == "terrain-nav-stale" )
        {
            if( !nav->IsOutOfDate( surface, true ) || mesh->GetData().SourceHash != s_hash ) { error = "Height editing did not mark the previous bake stale"; return false; } return true;
        }
        if( command == "terrain-nav-stable" )
        {
            if( nav->IsOutOfDate( surface, true ) || mesh->GetData().SourceHash != s_hash ) { error = "Paint, undo or visual LODs changed navigation geometry"; return false; } return true;
        }
        if( command != "terrain-nav-assert" ) { error = "Unknown terrain navigation command"; return false; }
        const auto info = nav->GetSurfaceInfo( surface );
        if( info.Baking || !info.LastError.empty() || nav->IsOutOfDate( surface, true ) || !std::filesystem::exists( surface.NavMeshData ) )
        { error = "Terrain navigation bake is not current: " + info.LastError; return false; }
        const auto origin = ground->GetComponent<Transform>().GetWorldPosition(); const float d = s_streamed ? 4.f : 12.f; std::vector<Vector3> corners;
        if( nav->FindPath( origin + Vector3( -d, 0, -d ), origin + Vector3( d, 0, d ), corners ) != NavPathStatus::Complete )
        { error = "Terrain path did not cross tile borders"; return false; }
        for( float p : { -d, 0.f, d } )
        {
            NavMeshHit hit; const auto expected = widget->GetActive()->GetField().Query( p, p );
            if( !nav->SamplePosition( origin + Vector3( p, float( expected.Height ), p ), 0.5f, hit ) || std::abs( hit.Position.y - origin.y - expected.Height ) > 0.3 )
            { error = "Navigation heights differ from canonical terrain"; return false; }
        }
        if( s_streamed && ( !terrain.GetData() || !terrain.GetData()->Heights->GetTiles().empty() ) )
        { error = "Offline terrain navigation populated the runtime canonical tile cache"; return false; }
        return true;
    }
}
#endif
