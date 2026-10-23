#include "TreeAutomation.h"
#if USING( ME_EDITOR )
#include "Components/Graphics/Model.h"
#include "Editor/EditorOperations.h"
#include "Editor/Selection.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Graphics/MeshData.h"
#include "Graphics/ModelResource.h"
#include "Procedural/Tree/TreeAsset.h"
#include "Procedural/Tree/TreePresets.h"
#include "Renderer.h"
#include "World/SceneSerializer.h"
#include "Widgets/Terrain/TerrainWidget.h"
#include <filesystem>
#include <map>

namespace TreeAutomation
{
    namespace
    {
        std::map<std::string, const ModelResource*> s_marks;
        int s_waitFrames = 0;


        std::vector<std::string> Split( const std::string& InArgs )
        {
            std::vector<std::string> parts;
            size_t start = 0;
            while( true )
            {
                const size_t bar = InArgs.find( '|', start );
                std::string part = InArgs.substr( start, bar == std::string::npos ? std::string::npos : bar - start );
                const size_t first = part.find_first_not_of( " \t" ), last = part.find_last_not_of( " \t" );
                parts.push_back( first == std::string::npos ? std::string() : part.substr( first, last - first + 1 ) );
                if( bar == std::string::npos ) break;
                start = bar + 1;
            }
            return parts;
        }


        Model* FindModel( const std::string& InName, std::string& OutError )
        {
            EntityHandle entity = EditorOps::GetWorld().FindEntityByName( InName );
            Model* model = entity ? entity->TryGetComponent<Model>() : nullptr;
            if( !model ) OutError = "No model entity named " + InName;
            return model;
        }
    }


    bool Execute( const std::string& InCommand, const std::string& InArgs, std::string& OutError, bool& OutRetry )
    {
        const std::vector<std::string> args = Split( InArgs );
        if( InCommand == "tree-new" )
        {
            MitchTree::TreeRecipe recipe;
            if( args.size() < 2 || !MitchTree::MakePreset( args[0], recipe ) ) { OutError = "Usage: tree-new <Preset> | <path.mtree>"; return false; }
            const Path path( args[1] );
            std::error_code error;
            std::filesystem::create_directories( std::filesystem::path( path.FullPath ).parent_path(), error );
            return MitchTree::SaveRecipe( path.FullPath, recipe, OutError );
        }
        if( InCommand == "tree-place" )
        {
            if( args.size() < 4 ) { OutError = "Usage: tree-place <path.mtree> | <Name> | x | z"; return false; }
            json entity;
            entity["Name"] = args[1];
            entity["Components"] = json::array( { json{ { "Type", "Transform" }, { "Position", { std::stof( args[2] ), 0.f, std::stof( args[3] ) } } },
                json{ { "Type", "Model" }, { "ModelPath", Path( args[0] ).GetLocalPathString() }, { "Compact", true } } } );
            json data;
            data["Version"] = SceneSerializer::kVersion;
            data["Entities"] = json::array( { entity } );
            if( EditorOps::CreateFromData( data, nullptr, "Place Tree" ).empty() ) { OutError = "Placement failed"; return false; }
            return true;
        }
        if( InCommand == "tree-set" )
        {
            if( args.size() < 3 ) { OutError = "Usage: tree-set <path.mtree> | <field> | <json>"; return false; }
            const Path path( args[0] );
            MitchTree::TreeRecipe recipe;
            if( !MitchTree::LoadRecipe( path.FullPath, recipe, OutError ) ) return false;
            const json value = json::parse( args[2], nullptr, false );
            if( value.is_discarded() || !Reflection::SetPathJson( MitchTree::TreeRecipe::StaticType(), &recipe, args[1], value ) ) { OutError = "Can't set " + args[1]; return false; }
            recipe.Sanitize();
            if( !MitchTree::SaveRecipe( path.FullPath, recipe, OutError ) ) return false;
            Model::ReloadAll( EditorOps::GetWorld(), path.FullPath );
            return true;
        }
        if( InCommand == "tree-foliage" )
        {
            TerrainWidget* terrain = TerrainWidget::Get();
            if( !terrain || args.empty() || !terrain->AddFoliageModel( Path( args[0] ).GetLocalPathString() ) ) { OutError = "Could not add the tree to the foliage palette"; return false; }
            return true;
        }

        if( args.empty() ) { OutError = "Missing entity name"; return false; }
        Model* model = FindModel( args[0], OutError );
        if( !model ) return false;
        if( InCommand == "tree-wait" )
        {
            if( model->IsReady() ) { s_waitFrames = 0; return true; }
            if( ++s_waitFrames > 1800 ) { s_waitFrames = 0; OutError = "Timed out waiting for " + args[0]; return false; }
            OutRetry = true;
            return true;
        }
        if( InCommand == "tree-mark" ) { s_marks[args[0]] = model->ModelHandle.get(); return true; }
        if( InCommand == "tree-assert-regenerated" )
        {
            if( !model->ModelHandle || model->ModelHandle.get() == s_marks[args[0]] ) { OutError = args[0] + " still uses its previous generation"; return false; }
            return true;
        }
        if( InCommand == "tree-assert" )
        {
            const size_t parts = args.size() > 1 ? static_cast<size_t>( std::stoul( args[1] ) ) : 2;
            if( !model->IsReady() || !model->ModelHandle || !model->ModelHandle->IsTree() ) { OutError = args[0] + " is not a loaded MitchTree"; return false; }
            if( model->GetRenderParts().size() != parts ) { OutError = args[0] + " has " + std::to_string( model->GetRenderParts().size() ) + " parts"; return false; }
            const uint64_t id = EditorOps::GetWorld().FindEntityByName( args[0] )->GetId().Value();
            size_t commands = 0;
            for( const auto& draw : GetEngine().GetRenderer().GetMeshCache().Commands )
            {
                if( !draw.SingleMesh || draw.ID != id ) continue;
                ++commands;
                if( !draw.SupportsInstancing ) { OutError = args[0] + " parts do not use the instancing path"; return false; }
                if( draw.SingleMesh->GetLodCount() == 0 && model->ModelHandle->GetTreeRecipe()->Lod.Levels > 0 ) { OutError = args[0] + " lost its levels of detail"; return false; }
            }
            if( commands != parts ) { OutError = args[0] + " submitted " + std::to_string( commands ) + " draws"; return false; }
            return true;
        }
        return false;
    }
}
#endif
