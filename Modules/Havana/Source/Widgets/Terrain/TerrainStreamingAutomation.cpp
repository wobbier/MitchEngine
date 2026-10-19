#include "TerrainStreamingAutomation.h"
#if USING( ME_EDITOR )
#include "TerrainWidget.h"
#include "Components/Graphics/Terrain.h"
#include "Components/Physics/CharacterController.h"
#include "Components/Camera.h"
#include "Components/Transform.h"
#include "Cores/TerrainCore.h"
#include "Cores/PhysicsCore.h"
#include "Cores/EditorCore.h"
#include "Editor/EditorOperations.h"
#include "Editor/Selection.h"
#include "EditorApp.h"
#include "Havana.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Terrain/TerrainStreamCache.h"
#include "Resource/ResourceCache.h"
#include "Utils/GUID.h"
#include "World/Scene.h"
#include <filesystem>
#include <fstream>
#include <cmath>

namespace TerrainStreamingAutomation
{
    namespace
    {
        std::filesystem::path s_directory;
        uint64_t s_loaded = 0;
        EntityHandle Ground() { return EditorOps::GetWorld().FindEntityByName( "Terrain" ); }
        EntityHandle Player() { return EditorOps::GetWorld().FindEntityByName( "Stream Player" ); }
        void Pose( float coordinate )
        {
            const Vector3 eye( coordinate, 25, coordinate - 24 );
            static_cast<EditorApp*>( GetEngine().GetGame() )->EditorSceneManager->GetCameraController().SetPose( eye, 30, 0 );
            auto camera = EditorOps::GetWorld().FindEntityByName( "Main Camera" ); if( camera )
            { camera->GetComponent<Transform>().SetPosition( eye ); camera->GetComponent<Transform>().SetRotation( Vector3( 30, 0, 0 ) ); }
        }
    }


    bool Execute( const std::string& command, const std::string& args, std::string& error )
    {
        auto* widget = TerrainWidget::Get();
        if( command == "terrain-stream-fixture" )
        {
            s_directory = std::filesystem::path( Path( ".tmp/Tests/TerrainStreamingFlows" ).FullPath ) / std::to_string( GUID::Generate() );
            TerrainData::Settings s; s.TileQuads = 16; s.PatchQuads = 8; s.MinTile = { -4, -4 }; s.TilesX = s.TilesZ = 9;
            TerrainData::Heightfield field( s );
            for( int x = -4; x <= 4; ++x ) for( int z = -4; z <= 4; ++z ) if( !field.CreateFlatTile( { x, z }, 0, error ) ) return false;
            for( int z = -64; z <= 80; ++z ) for( int x = -64; x <= 80; ++x ) if( !field.SetSample( { x, z }, float( 2 * std::sin( x * 0.13 ) * std::cos( z * 0.11 ) ), error ) ) return false;
            if( !widget->CreateFromField( ( s_directory / "Source.terrain" ).generic_string(), std::move( field ) ) ) return false;
            for( int i = 1; i <= 3; ++i ) if( auto box = EditorOps::GetWorld().FindEntityByName( "Terrain Box " + std::to_string( i ) ) ) box->MarkForDelete();
            auto actor = EditorOps::GetWorld().CreateEntity( "Stream Player" ); actor->AddComponent<Transform>().SetPosition( Vector3( -40, 12, -40 ) ); actor->AddComponent<CharacterController>();
            Pose( -40 ); return true;
        }
        if( command == "terrain-stream-cook" ) return widget->StartStreamCook( ( s_directory / "Runtime.terrainc" ).generic_string(), 2 );
        if( command == "terrain-stream-cook-wait" ) { if( !widget->FinishStreamCook( true ) ) { error = "Streamed terrain cook failed"; return false; } return true; }
        auto ground = Ground(); auto* terrain = ground ? ground->TryGetComponent<Terrain>() : nullptr;
        if( !terrain ) return false;
        if( command == "terrain-stream-bind" )
        {
            terrain->Asset = Path( ( s_directory / "Runtime.terrainc" ).generic_string() ); terrain->ReloadAsset();
            terrain->CpuBudgetMiB = terrain->GpuBudgetMiB = terrain->CollisionBudgetMiB = 1;
            terrain->StreamDistance = 24; terrain->CollisionDistance = 1; terrain->UploadKiB = 16; terrain->UploadMilliseconds = 1;
            terrain->ShowResidency = true; Selection::Get().Set( ground ); return true;
        }
        if( command == "terrain-stream-teleport" )
        {
            const float coordinate = std::stof( args ); auto actor = Player(); if( !actor ) return false;
            actor->GetComponent<CharacterController>().Teleport( Vector3( coordinate, 20, coordinate ), Quaternion() ); Pose( coordinate );
            GetEngine().Terrains->SyncNow();
            if( GetEngine().Terrains->IsCollisionReady() ) { error = "Teleport did not establish a collision readiness barrier"; return false; }
            return true;
        }
        if( command == "terrain-stream-camera" ) { Pose( std::stof( args ) ); return true; }
        if( command == "terrain-stream-wait" ) { terrain->WaitForStreamingRequest(); GetEngine().Physics->WaitForTerrainCollision(); return true; }
        const auto* stats = terrain->GetStreamingStats();
        if( !stats || !terrain->GetData() ) { error = "Streamed terrain has not loaded"; return false; }
        if( command == "terrain-stream-mark" ) { s_loaded = stats->Loaded; return true; }
        if( command == "terrain-stream-assert-evicted" )
        {
            if( !stats->Evicted || stats->Loaded <= s_loaded )
            {
                error = "Travel did not evict/load: CPU=" + std::to_string( stats->CpuReserved ) + " resident=" + std::to_string( stats->Resident ) + " retiring=" + std::to_string( stats->Retiring ) + " query pins=" + std::to_string( stats->QueryPins ) + " draw pins=" + std::to_string( stats->DrawPins ) + " collision pins=" + std::to_string( stats->CollisionPins ) + " / " + stats->Pressure; return false;
            }
        }
        else if( command == "terrain-stream-collision" )
        {
            auto actor = Player(); if( !actor ) return false; const auto position = actor->GetComponent<Transform>().GetWorldPosition();
            const auto expected = terrain->GetData()->Heights->Query( position.x + 2, position.z ); RaycastHit hit;
            const bool ray = GetEngine().Physics->Raycast( Vector3( position.x + 2, 40, position.z ), Vector3( 0, -1, 0 ), 100, hit );
            if( !GetEngine().Terrains->IsCollisionReady() || !GetEngine().Physics->IsTerrainCollisionReady() || expected.Status != TerrainData::QueryStatus::Ready ||
                !ray || hit.Entity != ground || std::abs( hit.Position.y - expected.Height ) > 1e-4 )
            {
                error = "Streamed collision: pages=" + std::to_string( GetEngine().Terrains->IsCollisionReady() ) + " shapes=" + std::to_string( GetEngine().Physics->IsTerrainCollisionReady() ) +
                    " query=" + std::to_string( int( expected.Status ) ) + " ray=" + std::to_string( ray ) + " actor=" + std::to_string( position.x ) + "," + std::to_string( position.z ) +
                    " actual=" + std::to_string( hit.Position.y ) + " expected=" + std::to_string( expected.Height ) + " tiles=" + std::to_string( terrain->GetData()->CollisionTiles.size() ) + " / " + stats->Pressure; return false;
            }
            return true;
        }
        else if( command == "terrain-stream-save" )
        {
            GetEngine().CurrentScene->Save( ( s_directory / "Runtime.lvl" ).generic_string(), EditorOps::GetSceneRoot() );
            std::ofstream( s_directory.parent_path() / "LatestScene.txt" ) << ( s_directory / "Runtime.lvl" ).generic_string(); return true;
        }
        else if( command != "terrain-stream-assert" ) { error = "Unknown terrain streaming command"; return false; }
        if( stats->Resident == 0 || stats->Resident >= 81 || stats->CpuReserved > 1024 * 1024 || stats->GpuReserved > 1024 * 1024 || stats->CollisionReserved > 1024 * 1024 || !stats->Error.empty() )
        { error = "Terrain streaming exceeded its budgets or failed: " + stats->Error + " / " + stats->Pressure; return false; }
        const auto& choices = terrain->GetPatchLods(); if( choices.size() != 324 ) { error = "Terrain streaming lost coarse patch coverage"; return false; }
        const auto& neighbors = terrain->GetData()->PatchNeighbors;
        for( size_t i = 0; i < choices.size(); ++i ) for( int32_t n : neighbors[i] ) if( n >= 0 && std::abs( int( choices[i].Level ) - int( choices[n].Level ) ) > 1 )
        { error = "Terrain detail/coarse boundary lost 2:1 continuity"; return false; }
        return true;
    }
}
#endif
