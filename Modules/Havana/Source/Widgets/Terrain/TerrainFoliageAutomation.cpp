#include "TerrainFoliageAutomation.h"
#if USING( ME_EDITOR )
#include "TerrainWidget.h"
#include "Components/Graphics/TerrainFoliage.h"
#include "Components/Graphics/Terrain.h"
#include "Components/Camera.h"
#include "Components/Transform.h"
#include "Materials/TerrainMaterial.h"
#include "Cores/EditorCore.h"
#include "Editor/EditorOperations.h"
#include "Editor/Selection.h"
#include "EditorApp.h"
#include "Engine/Engine.h"
#include "Renderer.h"
#include "Engine/World.h"
#include "Utils/GUID.h"
#include <cmath>

namespace TerrainFoliageAutomation
{
    namespace { json s_mark; size_t s_children = 0; }


    bool Execute( const std::string& command, const std::string& args, std::string& error )
    {
        auto* widget = TerrainWidget::Get(); auto& world = EditorOps::GetWorld(); auto ground = world.FindEntityByName( "Terrain" );
        if( !widget || !ground ) return false;
        if( command == "foliage-open" )
        {
            if( !widget->Open( ground->GetComponent<Terrain>().Asset.GetLocalPathString() ) ) { error = "Could not reopen the foliage terrain source"; return false; }
            Selection::Get().Set( ground ); widget->SetCategory( 3 ); s_children = ground->GetComponent<Transform>().GetChildren().size(); return true;
        }
        if( command == "foliage-fixture" )
        {
            const std::string path = args == "playground" ? "Assets/Terrain/FoliagePlayground.terrain" : ".tmp/Tests/Foliage/" + std::to_string( GUID::Generate() ) + "/Source.terrain";
            TerrainData::Settings s; s.TileQuads = 32; s.PatchQuads = 16; s.MinTile = { -1, -1 }; s.TilesX = s.TilesZ = 2;
            TerrainData::Heightfield field( s );
            for( int z = -1; z <= 0; ++z ) for( int x = -1; x <= 0; ++x ) if( !field.CreateFlatTile( { x, z }, 0, error ) ) return false;
            for( int z = -32; z <= 32; ++z ) for( int x = -32; x <= 32; ++x ) if( !field.SetSample( { x, z }, 0.6f * std::sin( x * 0.12f ) * std::cos( z * 0.12f ), error ) ) return false;
            if( !widget->CreateFromField( path, std::move( field ) ) ) return false;
            auto& terrain = ground->GetComponent<Terrain>(); terrain.Asset = Path( path ); terrain.ReloadAsset();
            auto& transform = ground->GetComponent<Transform>(); transform.SetPosition( {} ); transform.SetRotation( Quaternion() ); transform.SetScale( { 1, 1, 1 } );
            for( int n = 1; n <= 3; ++n ) if( auto box = world.FindEntityByName( "Terrain Box " + std::to_string( n ) ) ) box->MarkForDelete();
            const Vector3 eye( 0, 27, -38 ); static_cast<EditorApp*>( GetEngine().GetGame() )->EditorSceneManager->GetCameraController().SetPose( eye, 34, 0 );
            if( auto camera = world.FindEntityByName( "Main Camera" ) ) { camera->GetComponent<Transform>().SetPosition( eye ); camera->GetComponent<Transform>().SetRotation( { 34, 0, 0 } ); }
            Selection::Get().Set( ground ); widget->SetCategory( 3 ); if( !widget->AddFoliageStarters() ) return false;
            s_children = transform.GetChildren().size(); return true;
        }
        if( command == "foliage-radius" ) { auto brush = widget->FoliageBrush; brush.Radius = std::stof( args ); widget->SetFoliageBrush( brush ); return true; }
        if( command == "foliage-assert-radius" ) { if( std::abs( widget->FoliageBrush.Radius - std::stof( args ) ) > 0.001f ) { error = "Foliage brush radius undo failed"; return false; } return true; }
        if( command == "foliage-stroke" )
        {
            const auto v = json::parse( args ); auto& b = widget->FoliageBrush; b.SpeciesIndex = v.value( "Species", 0u ); b.Radius = v.value( "Radius", 6.f ); b.Density = v.value( "Density", 0.12f );
            b.MinSpacing = v.value( "Spacing", 1.5f ); b.MinScale = v.value( "MinScale", 0.8f ); b.MaxScale = v.value( "MaxScale", 1.3f ); b.Erase = v.value( "Erase", false ); b.AlignNormal = v.value( "Align", false );
            if( !widget->BeginFoliageStroke( v["From"][0].get<double>(), v["From"][1].get<double>() ) || !widget->MoveFoliageStroke( v["To"][0].get<double>(), v["To"][1].get<double>() ) ) { error = "Foliage stroke failed"; return false; }
            widget->EndFoliageStroke( v.value( "Cancel", false ) ); return true;
        }
        auto* foliage = ground->TryGetComponent<TerrainFoliage>(); if( !foliage ) { error = "Foliage component unavailable"; return false; }
        if( command == "foliage-mark" ) { s_mark = Foliage::Encode( foliage->GetData() ); return true; }
        if( command == "foliage-assert-mark" ) { if( s_mark != Foliage::Encode( foliage->GetData() ) ) { error = "Foliage undo/reopen changed stable instance records"; return false; } return true; }
        if( command == "foliage-clear" ) return widget->ClearFoliage( true );
        if( command == "foliage-reapply" ) return widget->ReprojectFoliage();
        if( command == "foliage-hide" || command == "foliage-show" ) { foliage->SetEnabled( command == "foliage-show" ); return true; }
        if( command == "foliage-distant" || command == "foliage-near" ) { foliage->DrawDistance = command == "foliage-distant" ? 1 : 250; return true; }
        size_t commands = 0;
        for( const auto& draw : GetEngine().GetRenderer().GetMeshCache().Commands )
            if( draw.SingleMesh && draw.ID == ground->GetId().Value() && !dynamic_cast<TerrainMaterial*>( draw.MeshMaterial.get() ) && ( draw.Visible || draw.CastShadows ) )
            { ++commands; if( !draw.SupportsInstancing ) { error = "Starter foliage did not use the instancing path"; return false; } }
        if( command == "foliage-assert-hidden" ) { if( commands ) { error = "Hidden/deleted foliage retained live draw commands"; return false; } return true; }
        if( command != "foliage-assert" ) return false;
        const auto& data = foliage->GetData();
        if( data.Instances.size() < 20 || !commands || !foliage->GetError().empty() || ground->GetComponent<Transform>().GetChildren().size() != s_children )
        { error = "Foliage not ready, failed, or expanded into child entities: " + foliage->GetError(); return false; }
        for( const auto& i : data.Instances )
        {
            const auto hit = widget->GetActive()->GetField().Query( i.Position.x, i.Position.z );
            if( hit.Status != TerrainData::QueryStatus::Ready || std::abs( hit.Height - i.Position.y ) > 0.001f ) { error = "Foliage did not follow canonical source heights"; return false; }
        }
        return true;
    }
}
#endif
