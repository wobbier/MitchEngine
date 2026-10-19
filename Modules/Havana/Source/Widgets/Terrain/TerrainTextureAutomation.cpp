#include "TerrainTextureAutomation.h"
#if USING( ME_EDITOR )
#include "TerrainWidget.h"
#include "Editor/EditorOperations.h"
#include "Components/Graphics/Terrain.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Renderer.h"
#include "Materials/TerrainMaterial.h"
#include "Core/CommandLine.h"
#include <SDL.h>
#include <imgui.h>
#include <fstream>
#include <sstream>

namespace TerrainTextureAutomation
{
    namespace
    {
        std::array<std::string, 3> s_paths;
        std::vector<uint16_t> s_vertices;
        std::array<uint16_t, 3> s_maps{};
        void WriteMap( unsigned kind, bool changed = false )
        {
            std::vector<uint8_t> bytes( 18 ); bytes[2] = 2; bytes[12] = bytes[14] = 32; bytes[16] = 32; bytes[17] = 0x28;
            for( unsigned z = 0; z < 32; ++z ) for( unsigned x = 0; x < 32; ++x )
            {
                const bool a = ( x / 4 + z / 4 ) % 2; std::array<uint8_t, 4> p;
                if( kind == 0 ) p = a != changed ? std::array<uint8_t, 4>{ 180, 130, 65, 255 } : std::array<uint8_t, 4>{ 55, 110, 30, 255 };
                else if( kind == 1 ) p = { uint8_t( a ? 165 : 90 ), 128, 255, 255 };
                else p = { uint8_t( a ? 255 : 150 ), uint8_t( a ? 80 : 210 ), 160, 255 };
                bytes.insert( bytes.end(), { p[2], p[1], p[0], p[3] } );
            }
            std::ofstream file( Path( s_paths[kind] ).FullPath, std::ios::binary ); file.write( reinterpret_cast<const char*>( bytes.data() ), bytes.size() );
        }


        std::vector<const TerrainMaterial*> Materials()
        {
            std::vector<const TerrainMaterial*> result;
            for( const auto& command : GetEngine().GetRenderer().GetMeshCache().Commands )
                if( command.SingleMesh ) if( auto* material = dynamic_cast<const TerrainMaterial*>( command.MeshMaterial.get() ) ) result.push_back( material );
            return result;
        }


        std::vector<uint16_t> Vertices()
        {
            std::vector<uint16_t> result;
            for( const auto& command : GetEngine().GetRenderer().GetMeshCache().Commands )
                if( command.SingleMesh && dynamic_cast<const TerrainMaterial*>( command.MeshMaterial.get() ) ) result.push_back( command.VertexBufferIdx );
            return result;
        }
    }


    bool Execute( const std::string& command, const std::string& args, std::string& error )
    {
        auto* widget = TerrainWidget::Get(); auto* document = widget ? widget->GetActive() : nullptr;
        if( !document ) { error = "Open a terrain source first"; return false; }
        if( command == "terrain-texture-fixture" )
        {
            const auto root = std::filesystem::path( document->GetPath() ).parent_path() / "Textures";
            std::filesystem::create_directories( Path( root.generic_string() ).FullPath );
            for( unsigned i = 0; i < 3; ++i ) { s_paths[i] = ( root / ( "Layer" + std::to_string( i ) + ".tga" ) ).generic_string(); WriteMap( i ); }
            auto surface = std::make_shared<TerrainData::Surface>(); surface->Layers[0].Color = { 1, 1, 1 }; surface->Layers[0].Metallic = 0.3f;
            return document->SetSurface( surface );
        }
        if( command == "terrain-texture-dialog" ) { widget->EditSurfaceLayer(); return true; }
        if( command == "terrain-texture-field" || command == "terrain-texture-apply" )
        {
            std::istringstream stream( args ); unsigned kind = 0; std::string state;
            if( command == "terrain-texture-field" && !( stream >> kind ) ) return false;
            if( !( stream >> state ) || kind >= 3 || ( state != "down" && state != "up" ) ) return false;
            const auto center = command == "terrain-texture-field" ? widget->GetLayerMapCenter( kind ) : widget->GetLayerApplyCenter();
            SDL_WarpMouseGlobal( static_cast<int>( center.x ), static_cast<int>( center.y ) );
            auto& io = ImGui::GetIO(); io.AddMousePosEvent( center.x, center.y ); io.AddMouseButtonEvent( 0, state == "down" ); return true;
        }
        if( command == "terrain-texture-type" )
        {
            const auto kind = std::stoul( args ); if( kind >= 3 ) return false;
            ImGui::GetIO().AddInputCharactersUTF8( s_paths[kind].c_str() ); return true;
        }
        if( command == "terrain-texture-assert-references" )
        {
            const auto surface = document->GetField().GetSurface();
            if( !surface || surface->Layers[0].BaseColorMap != s_paths[0] || surface->Layers[0].NormalMap != s_paths[1] || surface->Layers[0].OrmMap != s_paths[2] )
            { error = "Terrain texture dialog did not preserve all three references"; return false; }
            return true;
        }
        if( command == "terrain-texture-mark-vertices" ) { s_vertices = Vertices(); return !s_vertices.empty(); }
        if( command == "terrain-texture-assert-vertices" )
        { if( s_vertices != Vertices() ) { error = "Changing terrain texture maps rebuilt GPU vertices"; return false; } return true; }
        if( command == "terrain-texture-assert-ready" )
        {
            const auto materials = Materials(); if( materials.empty() ) { error = "No terrain material commands"; return false; }
            std::shared_ptr<Moonlight::TerrainTextureSet> shared;
            for( const auto* material : materials )
            {
                const auto& set = material->GetLayerTextures();
                if( !set || !set->IsReady() || !set->GetError().empty() ) { error = set ? set->GetError() : "Terrain textures were not requested"; return false; }
                if( shared && shared != set ) { error = "Terrain patches/instances did not share the texture pack"; return false; }
                shared = set;
                if( CommandLine::Has( "--terrain-texture-atlas" ) && set->GetLayout() != Moonlight::TerrainTextureLayout::Atlas ) { error = "Atlas fallback was not selected"; return false; }
            }
            return true;
        }
        if( command == "terrain-texture-change" || command == "terrain-texture-break" )
        {
            const auto materials = Materials(); if( materials.empty() || !materials.front()->GetLayerTextures() ) return false;
            for( unsigned i = 0; i < 3; ++i ) s_maps[i] = materials.front()->GetLayerTextures()->GetMap( i ).idx;
            if( command == "terrain-texture-break" ) { std::ofstream file( Path( s_paths[0] ).FullPath, std::ios::binary ); file << "broken texture"; }
            else WriteMap( 0, true );
            return true;
        }
        if( command == "terrain-texture-repair" ) { WriteMap( 0 ); return true; }
        if( command == "terrain-texture-assert-changed" || command == "terrain-texture-assert-retained" )
        {
            const auto materials = Materials(); if( materials.empty() || !materials.front()->GetLayerTextures() ) return false;
            const auto set = materials.front()->GetLayerTextures(); bool equal = true;
            for( unsigned i = 0; i < 3; ++i ) equal &= s_maps[i] == set->GetMap( i ).idx;
            if( command == "terrain-texture-assert-retained" ? ( !set->IsReady() || set->GetError().empty() || !equal ) : ( !set->IsReady() || !set->GetError().empty() || equal ) )
            { error = "Terrain texture reload did not replace/retain the expected complete pack"; return false; }
            return true;
        }
        error = "Unknown terrain texture automation command"; return false;
    }
}
#endif
