#include "TerrainPagingAutomation.h"
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
#include "Engine/Engine.h"
#include "Engine/ProjectSettings.h"
#include "Engine/World.h"
#include "Terrain/TerrainStreamCache.h"
#include "Terrain/TerrainSurface.h"
#include "Materials/TerrainMaterial.h"
#include "Renderer.h"
#include "Graphics/MeshData.h"
#include "Core/CommandLine.h"
#include "World/Scene.h"
#include "Utils/GUID.h"
#include <cmath>
#include <filesystem>
#include <fstream>

namespace TerrainPagingAutomation
{
    namespace
    {
        std::filesystem::path s_directory;
        uint64_t s_loaded = 0;
        std::shared_ptr<const TerrainData::Surface> s_surface;
        bool s_compressed = false, s_hierarchical = false;
        uint64_t s_budget = 1024 * 1024;
        EntityHandle Ground() { return EditorOps::GetWorld().FindEntityByName( "Terrain" ); }
        EntityHandle Player() { return EditorOps::GetWorld().FindEntityByName( "Paged Player" ); }
        void Pose( float coordinate )
        {
            const Vector3 eye( coordinate, 25, coordinate - 24 );
            static_cast<EditorApp*>( GetEngine().GetGame() )->EditorSceneManager->GetCameraController().SetPose( eye, 30, 0 );
            if( auto camera = EditorOps::GetWorld().FindEntityByName( "Main Camera" ) )
            { camera->GetComponent<Transform>().SetPosition( eye ); camera->GetComponent<Transform>().SetRotation( Vector3( 30, 0, 0 ) ); }
        }
    }


    bool Execute( const std::string& command, const std::string& args, std::string& error )
    {
        auto* widget = TerrainWidget::Get();
        if( command == "terrain-stream-paged-fixture" )
        {
            s_hierarchical = args == "hierarchical" || args == "hierarchical-painted";
            s_budget = ( s_hierarchical ? 8 : 1 ) * 1024 * 1024;
            s_compressed = s_hierarchical || args == "compressed" || args == "compressed-painted";
            const bool painted = args == "painted" || args == "compressed-painted" || args == "hierarchical-painted"; s_surface.reset();
            const std::string local = std::string( s_hierarchical ? ".tmp/Tests/TerrainHierarchyFlows/" : s_compressed ? ".tmp/Tests/TerrainCompressedFlows/" : ( painted ? ".tmp/Tests/TerrainStreamedSurfaceFlows/" : ".tmp/Tests/TerrainPagedCoverageFlows/" ) ) + std::to_string( GUID::Generate() );
            s_directory = Path( local ).FullPath;
            TerrainData::Settings s; s.TileQuads = 16; s.PatchQuads = 8; s.MinTile = { -20, -20 }; s.TilesX = s.TilesZ = 41;
            TerrainData::Heightfield field( s );
            for( int x = -20; x <= 20; ++x ) for( int z = -20; z <= 20; ++z ) if( !field.CreateFlatTile( { x, z }, 0, error ) ) return false;
            for( int z = -320; z <= 336; ++z ) for( int x = -320; x <= 336; ++x ) if( !field.SetSample( { x, z }, float( 2 * std::sin( x * 0.13 ) * std::cos( z * 0.11 ) ), error ) ) return false;
            if( painted )
            {
                auto surface = std::make_shared<TerrainData::Surface>(); surface->BaseWeights = { 90, 80, 60, 25 };
                std::filesystem::create_directories( s_directory );
                for( unsigned kind = 0; kind < 3; ++kind )
                {
                    const auto name = "Map" + std::to_string( kind ) + ".tga";
                    std::vector<uint8_t> bytes( 18 ); bytes[2] = 2; bytes[12] = bytes[14] = 32; bytes[16] = 32; bytes[17] = 0x28;
                    for( unsigned z = 0; z < 32; ++z ) for( unsigned x = 0; x < 32; ++x )
                    {
                        const bool check = ( x / 4 + z / 4 ) % 2;
                        const std::array<uint8_t, 3> rgb = kind == 0 ? ( check ? std::array<uint8_t, 3>{ 255, 240, 200 } : std::array<uint8_t, 3>{ 130, 120, 100 } ) :
                            ( kind == 1 ? std::array<uint8_t, 3>{ uint8_t( check ? 165 : 90 ), 128, 255 } : std::array<uint8_t, 3>{ 220, 180, 128 } );
                        bytes.insert( bytes.end(), { rgb[2], rgb[1], rgb[0], 255 } );
                    }
                    std::ofstream file( s_directory / name, std::ios::binary ); file.write( reinterpret_cast<const char*>( bytes.data() ), bytes.size() );
                    for( auto& layer : surface->Layers )
                    {
                        auto& ref = kind == 0 ? layer.BaseColorMap : ( kind == 1 ? layer.NormalMap : layer.OrmMap ); ref = local + "/" + name;
                        layer.TileSize = 2.5f; layer.NormalStrength = 0.5f; layer.Metallic = 0.2f; layer.FlipNormalY = true;
                    }
                }
                for( int z = -320; z <= 336; ++z ) for( int x = -320; x <= 336; ++x )
                    if( x % 8 == 0 || z % 13 == 0 ) surface->Set( { x, z }, x % 16 == 0 ? TerrainData::LayerWeights{ 0, 0, 255, 0 } : TerrainData::LayerWeights{ 0, 255, 0, 0 } );
                if( !field.SetSurface( surface, error ) ) return false;
                s_surface = surface;
            }
            if( !widget->CreateFromField( ( s_directory / "Source.terrain" ).generic_string(), std::move( field ) ) ) return false;
            for( int i = 1; i <= 3; ++i ) if( auto box = EditorOps::GetWorld().FindEntityByName( "Terrain Box " + std::to_string( i ) ) ) box->MarkForDelete();
            auto actor = EditorOps::GetWorld().CreateEntity( "Paged Player" ); actor->AddComponent<Transform>().SetPosition( Vector3( -40, 12, -40 ) ); actor->AddComponent<CharacterController>();
            Pose( -40 );
            auto& settings = ProjectSettings::Get(); settings.TerrainCpuBudgetMiB = settings.TerrainGpuBudgetMiB = settings.TerrainCollisionBudgetMiB = int( s_budget / ( 1024 * 1024 ) ); settings.TerrainMaxLoads = 1;
            return widget->StartStreamCook( ( s_directory / "Runtime.terrainc" ).generic_string(), 2, true, s_compressed, s_hierarchical );
        }
        if( command == "terrain-stream-paged-cook-wait" ) return widget->FinishStreamCook( true );
        auto ground = Ground(); auto* terrain = ground ? ground->TryGetComponent<Terrain>() : nullptr;
        if( !terrain ) return false;
        if( command == "terrain-stream-paged-bind" )
        {
            terrain->Asset = Path( ( s_directory / "Runtime.terrainc" ).generic_string() ); terrain->ReloadAsset();
            terrain->CpuBudgetMiB = terrain->GpuBudgetMiB = terrain->CollisionBudgetMiB = int( s_budget / ( 1024 * 1024 ) );
            terrain->StreamDistance = 12; terrain->CoverageDistance = s_hierarchical ? 256 : 24; terrain->CollisionDistance = 1;
            terrain->UploadKiB = 16; terrain->UploadMilliseconds = 1; terrain->ShowResidency = !s_surface; Selection::Get().Set( ground ); return true;
        }
        if( command == "terrain-stream-paged-wait" ) { terrain->WaitForStreamingRequest(); GetEngine().Physics->WaitForTerrainCollision(); return true; }
        if( command == "terrain-stream-paged-camera" ) { Pose( std::stof( args ) ); return true; }
        if( command == "terrain-stream-paged-coarse-only" ) { terrain->StreamDetail = false; return true; }
        if( command == "terrain-stream-paged-teleport" )
        {
            const float coordinate = std::stof( args ); Player()->GetComponent<CharacterController>().Teleport( Vector3( coordinate, 20, coordinate ), Quaternion() ); Pose( coordinate );
            GetEngine().Terrains->SyncNow();
            if( GetEngine().Terrains->IsCollisionReady() ) { error = "Paged teleport skipped the collision barrier"; return false; } return true;
        }
        if( command == "terrain-stream-paged-save" )
        {
            GetEngine().CurrentScene->Save( ( s_directory / "Runtime.lvl" ).generic_string(), EditorOps::GetSceneRoot() );
            std::ofstream( s_directory.parent_path() / "LatestScene.txt" ) << ( s_directory / "Runtime.lvl" ).generic_string(); return true;
        }
        const auto* stats = terrain->GetStreamingStats(); const auto data = terrain->GetData();
        if( !stats || !data || !data->PagedCoverage ) { error = "Paged runtime terrain has not loaded"; return false; }
        if( command == "terrain-stream-paged-assert-hierarchy" )
        {
            if( !data->Hierarchical || data->CoverageNodes.empty() || !stats->HierarchyRoots || data->CoverageNodes.size() > 1024 ) { error = "Hierarchy coverage was not published"; return false; }
            std::shared_ptr<const TerrainData::StreamArchive> archive;
            if( !TerrainData::OpenStreamArchive( s_directory / "Runtime.terrainc", archive, error ) ) return false;
            uint64_t area = 0; bool parent = false;
            for( auto id : data->CoverageNodes ) { const auto level = archive->Nodes[id].Level; area += uint64_t( 1 ) << ( 2 * level ); parent |= level != 0; }
            if( area != uint64_t( archive->Config.TilesX ) * archive->Config.TilesZ || !parent ) { error = "Hierarchy lost distant coverage or expanded to all leaf pages"; return false; }
            if( data->Patches.size() >= archive->Blocks.size() * 4 ) { error = "Hierarchy did not reduce the visual footprint"; return false; }
            CLog::GetInstance().Log( CLog::LogType::Info, "[terrain-hierarchy] roots=" + std::to_string( stats->HierarchyRoots ) + " selected=" + std::to_string( data->CoverageNodes.size() ) + " patches=" + std::to_string( data->Patches.size() ) + " cpu=" + std::to_string( stats->CpuReserved ) + " gpu=" + std::to_string( stats->GpuReserved ) );
            return true;
        }
        if( command == "terrain-stream-paged-mark" ) { s_loaded = stats->CoverageLoaded; return true; }
        if( command == "terrain-stream-paged-assert-compressed" )
        {
            std::shared_ptr<const TerrainData::StreamArchive> archive;
            if( !TerrainData::OpenStreamArchive( s_directory / "Runtime.terrainc", archive, error ) ) return false;
            uint64_t stored = 0, decoded = 0;
            for( const auto& block : archive->Blocks ) { stored += block.Size + block.CoverageSize; decoded += block.DecodedSize + block.CoverageDecodedSize; }
            if( !archive->Compressed || ( !archive->Hierarchical && !archive->Coverage->Patches.empty() ) || stored >= decoded || !stats->BytesRead || stats->BytesRead >= stats->DecodedBytesRead )
            { error = "Compressed pages did not reduce file/read payloads or opened resident data"; return false; }
            CLog::Log( CLog::LogType::Info, "Compressed terrain payloads: " + std::to_string( stored ) + " stored / " + std::to_string( decoded ) + " decoded bytes" );
            return true;
        }
        if( command == "terrain-stream-paged-evicted" )
        {
            if( !stats->CoverageEvicted || stats->CoverageLoaded <= s_loaded ) { error = "Coverage travel did not load/evict pages"; return false; }
        }
        else if( command != "terrain-stream-paged-assert" ) { error = "Unknown paged terrain command"; return false; }
        const auto& world = GetEngine().Terrains->GetStreamingStats();
        if( world.OverBudget || world.CpuReserved > s_budget || world.GpuReserved > s_budget || world.CollisionReserved > s_budget || !stats->Error.empty() || !stats->HorizonReady || data->Patches.empty() || ( !s_hierarchical && data->CoverageTiles.size() >= 81 ) )
        { error = "Paged horizon failed: " + stats->Error + " / " + stats->Pressure + " CPU=" + std::to_string( world.CpuReserved ) + " pages=" + std::to_string( stats->CoverageResident ) + " pending=" + std::to_string( stats->CoveragePending ); return false; }
        const auto& choices = terrain->GetPatchLods();
        if( choices.size() != data->Patches.size() ) { error = "Paged GPU window has not completed"; return false; }
        for( size_t i = 0; i < choices.size(); ++i ) for( int32_t n : data->PatchNeighbors[i] ) if( n >= 0 && std::abs( int( choices[i].Level ) - int( choices[n].Level ) ) > 1 ) { error = "Paged LODs lost 2:1 continuity"; return false; }
        if( s_surface )
        {
            const auto palette = data->Heights->GetSurface();
            if( !palette || !palette->Samples.empty() || palette->Layers != s_surface->Layers || palette->BaseWeights != s_surface->BaseWeights )
            { error = "Streamed palette did not survive cooking/publication"; return false; }
            const auto q = data->Heights->GetSettings().PatchQuads;
            const auto expectedWeights = [&]( const TerrainData::ResidentPatch& patch ) {
                std::vector<TerrainData::LayerWeights> result;
                for( uint32_t z = 0; z <= q; ++z ) for( uint32_t x = 0; x <= q; ++x ) result.push_back( s_surface->Get( { patch.Mesh.SampleOrigin.X + ( int64_t( x ) << patch.SpanLevel ), patch.Mesh.SampleOrigin.Z + ( int64_t( z ) << patch.SpanLevel ) } ) );
                return result;
            };
            for( const auto& patch : data->CoveragePatches ) if( patch->Weights != expectedWeights( *patch ) )
            { error = "Streamed weights differ from authored canonical samples"; return false; }
            size_t count = 0; std::shared_ptr<Moonlight::TerrainTextureSet> shared;
            const auto origin = ground->GetComponent<Transform>().GetWorldPosition();
            for( const auto& command : GetEngine().GetRenderer().GetMeshCache().Commands )
            {
                if( !command.SingleMesh || command.ID != ground->GetId().Value() ) continue;
                auto* material = dynamic_cast<TerrainMaterial*>( command.MeshMaterial.get() ); if( !material ) continue;
                ++count; const auto& set = material->GetLayerTextures();
                const TerrainData::SampleCoord sample{ std::llround( command.Transform[3].x - origin.x ), std::llround( command.Transform[3].z - origin.z ) };
                const auto expected = std::find_if( data->CoveragePatches.begin(), data->CoveragePatches.end(), [&]( const auto& p ) { return p->Mesh.SampleOrigin == sample; } );
                if( expected == data->CoveragePatches.end() || std::abs( command.SingleMesh->Bounds.GetSize().x - ( *expected )->Mesh.Bounds.GetSize().x ) > 0.001f ||
                    !material->LayersEnabled || material->GetWeights() != expectedWeights( **expected ) || !set || !set->IsReady() || !set->GetError().empty() )
                { error = "Streamed GPU weights/textures do not match the authored surface"; return false; }
                if( shared && shared != set ) { error = "Streamed patches did not share layer texture packs"; return false; } shared = set;
                if( CommandLine::Has( "--terrain-texture-atlas" ) && set->GetLayout() != Moonlight::TerrainTextureLayout::Atlas )
                { error = "Streamed surface did not select the atlas fallback"; return false; }
            }
            if( count != data->Patches.size() )
            {
                error = "Painted GPU window has not completed: commands=" + std::to_string( count ) + " CPU patches=" + std::to_string( data->Patches.size() ) +
                    " LODs=" + std::to_string( terrain->GetPatchLods().size() ) + " prefetch=" + std::to_string( stats->PrefetchResident ) +
                    " / " + terrain->GetLodError() + " / " + stats->Pressure; return false;
            }
        }
        if( !terrain->StreamDetail && ( stats->Detail || data->Heights->GetTiles().size() != 1 ) ) { error = "Coarse coverage loaded unrelated canonical heights"; return false; }
        const auto position = Player()->GetComponent<Transform>().GetWorldPosition(); const auto expected = data->Heights->Query( position.x + 2, position.z ); RaycastHit hit;
        if( !GetEngine().Terrains->IsCollisionReady() || !GetEngine().Physics->IsTerrainCollisionReady() || expected.Status != TerrainData::QueryStatus::Ready ||
            !GetEngine().Physics->Raycast( Vector3( position.x + 2, 40, position.z ), Vector3( 0, -1, 0 ), 100, hit ) || hit.Entity != ground || std::abs( hit.Position.y - expected.Height ) > 1e-4 )
        { error = "Paged terrain lost canonical gameplay collision"; return false; }
        return true;
    }
}
#endif
