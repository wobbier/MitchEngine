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
#include "TreeDocument.h"
#include "TreePreview.h"
#include "TreeWidget.h"
#include <algorithm>
#include <filesystem>
#include <map>

namespace TreeAutomation
{
    namespace
    {
        std::map<std::string, const ModelResource*> s_marks;
        int s_waitFrames = 0;
        // tree-wait-preview: rendered frames when the document finished generating (0 = not yet).
        uint32_t s_previewSettled = 0;
        bool s_captureRequested = false;


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
        if( InCommand == "tree-open" )
        {
            if( args.empty() || !TreeWidget::Get() || !TreeWidget::Get()->Open( args[0] ) ) { OutError = "Could not open " + ( args.empty() ? std::string() : args[0] ); return false; }
            s_previewSettled = 0;
            return true;
        }
        if( InCommand == "tree-close" )
        {
            if( args.empty() || !TreeWidget::Get() || !TreeWidget::Get()->Close( args[0] ) ) { OutError = "No open tree " + ( args.empty() ? std::string() : args[0] ); return false; }
            return true;
        }
        if( InCommand == "tree-assert-open" )
        {
            if( args.size() < 2 || !TreeWidget::Get() ) { OutError = "Usage: tree-assert-open <path.mtree> | <0|1>"; return false; }
            const bool open = TreeWidget::Get()->Find( args[0] ) != nullptr;
            if( open != ( args[1] == "1" ) ) { OutError = args[0] + ( open ? " is open in MitchTree" : " isn't open in MitchTree" ); return false; }
            return true;
        }
        const bool documentAssert = InCommand.rfind( "tree-assert-", 0 ) == 0 && InCommand != "tree-assert-regenerated";
        if( documentAssert || InCommand == "tree-edit" || InCommand == "tree-preset" || InCommand == "tree-seed" || InCommand == "tree-undo" || InCommand == "tree-redo"
            || InCommand == "tree-save" || InCommand == "tree-preview" || InCommand == "tree-wait-preview" || InCommand == "tree-screenshot" || InCommand == "tree-page"
            || InCommand == "tree-level" )
        {
            TreeDocument* document = TreeWidget::Get() ? TreeWidget::Get()->GetActive() : nullptr;
            if( !document ) { OutError = "No tree is open in MitchTree"; return false; }
            TreePreview& preview = document->GetPreview();
            if( InCommand == "tree-edit" )
            {
                if( args.size() < 2 ) { OutError = "Usage: tree-edit <field> | <json>"; return false; }
                const json value = json::parse( args[1], nullptr, false );
                if( value.is_discarded() || !document->SetField( args[0], value ) ) { OutError = "Can't set " + args[0]; return false; }
                s_previewSettled = 0;
                return true;
            }
            if( InCommand == "tree-preset" )
            {
                const std::vector<std::string>& names = MitchTree::PresetNames();
                if( args.empty() || std::find( names.begin(), names.end(), args[0] ) == names.end() ) { OutError = "Unknown preset"; return false; }
                document->ApplyPreset( args[0] );
                s_previewSettled = 0;
                return true;
            }
            if( InCommand == "tree-seed" ) { document->RandomizeSeed(); s_previewSettled = 0; return true; }
            if( InCommand == "tree-undo" ) { document->GetUndo().Undo(); s_previewSettled = 0; return true; }
            if( InCommand == "tree-redo" ) { document->GetUndo().Redo(); s_previewSettled = 0; return true; }
            if( InCommand == "tree-page" )
            {
                const int level = args.size() > 1 ? std::stoi( args[1] ) - 1 : 0;
                if( args.empty() || !document->ShowPage( args[0], level ) ) { OutError = "Usage: tree-page <Tree|Trunk|Branches|Leaves|...> [| level]"; return false; }
                return true;
            }
            if( InCommand == "tree-level" )
            {
                const int levels = static_cast<int>( document->GetRecipe().Branches.size() );
                const int level = args.size() > 1 ? std::stoi( args[1] ) - 1 : levels - 1;
                if( args.empty() || ( args[0] != "add" && ( level < 0 || level >= levels ) ) ) { OutError = "Usage: tree-level <add|remove|toggle> [| level]"; return false; }
                if( args[0] == "add" ) document->AddLevel();
                else if( args[0] == "remove" ) document->RemoveLevel( level );
                else if( args[0] == "toggle" ) document->ToggleLevel( level );
                else { OutError = "Unknown tree-level action " + args[0]; return false; }
                s_previewSettled = 0;
                return true;
            }
            if( InCommand == "tree-assert-levels" )
            {
                // tree-assert-levels <levels> [| <enabled>]
                const std::vector<MitchTree::BranchLevel>& branches = document->GetRecipe().Branches;
                const int enabled = static_cast<int>( std::count_if( branches.begin(), branches.end(), []( const MitchTree::BranchLevel& InLevel ) { return InLevel.Enabled; } ) );
                if( args.empty() || static_cast<int>( branches.size() ) != std::stoi( args[0] ) || ( args.size() > 1 && enabled != std::stoi( args[1] ) ) )
                {
                    OutError = "The recipe has " + std::to_string( branches.size() ) + " branch levels (" + std::to_string( enabled ) + " on)";
                    return false;
                }
                return true;
            }
            if( InCommand == "tree-save" )
            {
                if( !document->Save() ) { OutError = "Save failed"; return false; }
                return true;
            }
            if( InCommand == "tree-preview" )
            {
                if( args.empty() ) { OutError = "Usage: tree-preview <option> | <value>"; return false; }
                const std::string& option = args[0];
                const float value = args.size() > 1 && !args[1].empty() ? std::stof( args[1] ) : 0.f;
                if( option == "wind" ) preview.Wind = value != 0.f;
                else if( option == "lod" ) preview.ForcedLod = static_cast<int>( value );
                else if( option == "wireframe" ) preview.Wireframe = value != 0.f;
                else if( option == "ground" ) preview.ShowGround = value != 0.f;
                else if( option == "yaw" ) preview.Yaw = value;
                else if( option == "pitch" ) preview.Pitch = value;
                else if( option == "distance" ) preview.Distance = value;
                else if( option == "frame" ) preview.Frame();
                else if( option == "scene" ) preview.SceneLighting = value != 0.f;
                else if( option == "sun" ) preview.SunAzimuth = value;
                else if( option == "sun-height" ) preview.SunElevation = value;
                else { OutError = "Unknown preview option " + option; return false; }
                return true;
            }
            if( InCommand == "tree-wait-preview" )
            {
                // Generated, then shown for a few frames (textures, shadows and exposure settle).
                if( !document->IsGenerating() && preview.HasMeshes() )
                {
                    if( s_previewSettled == 0 ) s_previewSettled = preview.GetRenderedFrames() + 1;
                    if( preview.GetRenderedFrames() >= s_previewSettled + 8 ) { s_waitFrames = 0; return true; }
                }
                if( ++s_waitFrames > 3600 ) { s_waitFrames = 0; OutError = "Timed out waiting for the MitchTree preview"; return false; }
                OutRetry = true;
                return true;
            }
            if( InCommand == "tree-assert-stats" )
            {
                if( args.size() < 2 ) { OutError = "Usage: tree-assert-stats <min> | <max>"; return false; }
                const std::vector<uint32_t>& triangles = document->GetStatistics().Triangles;
                if( triangles.empty() ) { OutError = "No statistics yet"; return false; }
                const uint32_t low = static_cast<uint32_t>( std::stoul( args[0] ) ), high = static_cast<uint32_t>( std::stoul( args[1] ) );
                if( triangles[0] < low || triangles[0] > high ) { OutError = "Full detail has " + std::to_string( triangles[0] ) + " triangles"; return false; }
                for( size_t lod = 1; lod < triangles.size(); ++lod )
                {
                    if( triangles[lod] >= triangles[lod - 1] ) { OutError = "LOD " + std::to_string( lod ) + " is not smaller than LOD " + std::to_string( lod - 1 ); return false; }
                }
                return true;
            }
            if( InCommand == "tree-assert-dirty" )
            {
                const bool expected = !args.empty() && args[0] == "1";
                if( document->IsDirty() != expected ) { OutError = expected ? "The tree should have unsaved changes" : "The tree should be saved"; return false; }
                return true;
            }
            if( InCommand == "tree-assert-lod" )
            {
                if( args.empty() || preview.GetDrawnLod() != static_cast<uint8_t>( std::stoul( args[0] ) ) ) { OutError = "The preview drew LOD " + std::to_string( preview.GetDrawnLod() ); return false; }
                return true;
            }
            if( InCommand == "tree-screenshot" )
            {
                if( args.empty() ) { OutError = "Usage: tree-screenshot <path.png>"; return false; }
                if( !s_captureRequested )
                {
                    std::error_code error;
                    std::filesystem::create_directories( std::filesystem::path( Path( args[0] ).FullPath ).parent_path(), error );
                    preview.RequestCapture( Path( args[0] ).FullPath );
                    s_captureRequested = true;
                    OutRetry = true;
                    return true;
                }
                if( !preview.IsCaptureDone() ) { OutRetry = true; return true; }
                s_captureRequested = false;
                if( !preview.GetCaptureError().empty() ) { OutError = preview.GetCaptureError(); return false; }
                return true;
            }
            OutError = "Unknown command";
            return false;
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
