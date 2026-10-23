#include "TreeDocument.h"

#if USING( ME_EDITOR )
#include "TreePreview.h"
#include "CLog.h"
#include "Components/Graphics/Model.h"
#include "Editor/EditorOperations.h"
#include "Editor/ReflectionUI.h"
#include "Graphics/MeshData.h"
#include "Graphics/Texture.h"
#include "ImGuiTheme.h"
#include "Path.h"
#include "Procedural/Tree/TreeAsset.h"
#include "Procedural/Tree/TreeModel.h"
#include "Procedural/Tree/TreePresets.h"
#include "Procedural/Tree/TreeTextures.h"
#include "Resource/ResourceCache.h"
#include "imgui_internal.h"
#include "misc/cpp/imgui_stdlib.h"
#include <bimg/bimg.h>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <random>

namespace
{
    // An edit of the recipe as before/after snapshots. Continuous drags of one field fold into one step.
    class RecipeEdit final
        : public UndoCommand
    {
    public:
        RecipeEdit( TreeDocument& InDocument, json InBefore, json InAfter, std::string InName, std::string InKey, bool InOpen )
            : m_document( InDocument ), m_before( std::move( InBefore ) ), m_after( std::move( InAfter ) ), m_name( std::move( InName ) ), m_key( std::move( InKey ) ), m_open( InOpen )
        {
        }

        void Undo() override { m_document.ApplySnapshot( m_before ); }
        void Redo() override { m_document.ApplySnapshot( m_after ); }
        std::string GetName() const override { return m_name; }
        bool MergeWith( const UndoCommand& InNext ) override
        {
            const auto* next = dynamic_cast<const RecipeEdit*>( &InNext );
            if( !next || !m_open || next->m_key != m_key ) return false;
            m_after = next->m_after;
            m_open = next->m_open;
            return true;
        }

    private:
        TreeDocument& m_document;
        json m_before;
        json m_after;
        std::string m_name;
        std::string m_key;
        bool m_open = false;
    };

    const char* kCrownNames[] = { "Conical", "Spherical", "Hemispherical", "Cylindrical", "Tapered Cylindrical", "Flame", "Inverse Conical", "Tend Flame" };

    // Generated texture channels with the recipe settings each is baked from.
    std::string TextureKey( const MitchTree::TreeRecipe& InRecipe, const std::string& InChannel )
    {
        const json recipe = InRecipe.ToJson();
        if( InChannel.rfind( "bark-", 0 ) == 0 ) return recipe["BarkTextures"].dump();
        // Leaf mips keep their alpha-tested coverage at the cutoff.
        return recipe["LeafTextures"].dump() + ( InChannel == "leaf-color" ? std::to_string( InRecipe.Foliage.AlphaCutoff ) : std::string() );
    }


    bool UsesChannel( const MitchTree::TreeRecipe& InRecipe, const std::string& InChannel )
    {
        if( InChannel.rfind( "bark-", 0 ) == 0 ) return InRecipe.BarkTextures.Generate;
        return InRecipe.LeafTextures.Generate && InRecipe.Leaves.Kind != MitchTree::LeafKind::None;
    }
}


TreeDocument::TreeDocument( const std::string& InPath )
    : m_path( Path( InPath ).GetLocalPathString() )
    , m_fullPath( Path( InPath ).FullPath )
    , m_preview( std::make_unique<TreePreview>() )
{
    m_title = std::filesystem::path( m_path ).stem().string();
}


TreeDocument::~TreeDocument()
{
    if( m_job.valid() )
    {
        JobResult result = m_job.get();
        for( auto& [channel, image] : result.Images ) if( image ) bimg::imageFree( image );
    }
}


bool TreeDocument::Load( std::string* OutError )
{
    std::string error;
    MitchTree::TreeRecipe recipe;
    if( !MitchTree::LoadRecipe( m_fullPath, recipe, error ) )
    {
        if( OutError ) *OutError = error;
        return false;
    }
    m_recipe = recipe;
    m_committed = m_recipe.ToJson();
    m_undo.Clear();
    m_undo.MarkSaved();
    RequestRegenerate();
    return true;
}


bool TreeDocument::Save()
{
    std::string error;
    if( !MitchTree::SaveRecipe( m_fullPath, m_recipe, error ) )
    {
        YIKES( "Couldn't save " + m_path + ": " + error );
        return false;
    }
    m_undo.MarkSaved();
    m_status = "Saved";
    // Placed instances regenerate now (the asset watcher only sees files under Assets).
    Model::ReloadAll( EditorOps::GetWorld(), m_fullPath );
    return true;
}


void TreeDocument::ApplySnapshot( const json& InRecipe )
{
    std::string error;
    MitchTree::TreeRecipe recipe;
    if( !recipe.FromJson( InRecipe, error ) )
    {
        YIKES( "MitchTree: " + error );
        return;
    }
    m_recipe = recipe;
    m_committed = m_recipe.ToJson();
    RequestRegenerate();
}


void TreeDocument::Commit( const std::string& InName, const std::string& InKey )
{
    m_recipe.Sanitize();
    json after = m_recipe.ToJson();
    if( after == m_committed ) return;
    // While a control is held (dragging a slider) later edits of it fold into this step.
    m_undo.Push( std::make_unique<RecipeEdit>( *this, m_committed, after, InName, InKey, ImGui::GetCurrentContext() && ImGui::IsAnyItemActive() ) );
    m_committed = std::move( after );
    RequestRegenerate();
}


bool TreeDocument::SetField( const std::string& InPath, const json& InValue )
{
    if( !Reflection::SetPathJson( MitchTree::TreeRecipe::StaticType(), &m_recipe, InPath, InValue ) ) return false;
    Commit( "Edit " + InPath, "set:" + InPath );
    return true;
}


void TreeDocument::ApplyPreset( const std::string& InPreset )
{
    MitchTree::TreeRecipe preset;
    if( !MitchTree::MakePreset( InPreset, preset ) ) return;
    preset.Name = m_recipe.Name;
    m_recipe = preset;
    Commit( "Apply " + InPreset, "preset" );
    m_framed = false;
}


void TreeDocument::RandomizeSeed()
{
    std::random_device device;
    m_recipe.Seed = device() % 100000u + 1u;
    Commit( "Randomize Seed", "seed" );
}


void TreeDocument::RequestRegenerate()
{
    m_regenerate = true;
}


void TreeDocument::StartJob()
{
    m_regenerate = false;
    std::vector<std::string> channels;
    for( const std::string& channel : MitchTree::TextureChannels() )
    {
        if( !UsesChannel( m_recipe, channel ) ) continue;
        auto key = m_textureKeys.find( channel );
        if( key == m_textureKeys.end() || key->second != TextureKey( m_recipe, channel ) ) channels.push_back( channel );
    }
    MitchTree::TreeRecipe recipe = m_recipe;
    m_job = std::async( std::launch::async, [recipe, channels]() {
        JobResult result;
        result.Recipe = recipe;
        result.Mesh = MitchTree::Generate( recipe );
        for( const std::string& channel : channels )
        {
            std::string error;
            result.Images[channel] = MitchTree::BakeChannel( recipe, channel, error );
            if( !result.Images[channel] ) CLog::Log( CLog::LogType::Error, "MitchTree texture " + channel + ": " + error );
        }
        return result;
    } );
}


void TreeDocument::FinishJob()
{
    JobResult result = m_job.get();
    for( auto& [channel, image] : result.Images )
    {
        const bool bark = channel.rfind( "bark-", 0 ) == 0;
        if( image )
        {
            m_textures[channel] = Moonlight::Texture::CreateFromImage( m_path + "#" + channel, image, bark ? Moonlight::WrapMode::Wrap : Moonlight::WrapMode::Clamp );
        }
        m_textureKeys[channel] = TextureKey( result.Recipe, channel );
    }
    const std::string folder = std::filesystem::path( m_fullPath ).parent_path().string();
    auto textures = [this, &folder]( const std::string& InSource, bool InGenerated, Moonlight::WrapMode InWrap ) -> SharedPtr<Moonlight::Texture> {
        if( InGenerated )
        {
            auto found = m_textures.find( InSource );
            return found != m_textures.end() ? found->second : nullptr;
        }
        const Path relative( folder + "/" + InSource );
        const Path path = relative.Exists ? relative : Path( InSource );
        return path.Exists ? ResourceCache::GetInstance().GetAsync<Moonlight::Texture>( path, InWrap ) : nullptr;
    };
    m_preview->SetMeshes( MitchTree::BuildMeshes( result.Mesh, result.Recipe, textures ), !m_framed );
    m_framed = true;
    m_statistics = result.Mesh.Statistics;
    m_status.clear();
}


bool TreeDocument::ClaimsAction( const std::string& InId ) const
{
    return m_isFocused && ( InId == "File.Save" || InId == "Edit.Undo" || InId == "Edit.Redo" );
}


void TreeDocument::ExecuteAction( const std::string& InId )
{
    if( InId == "File.Save" ) Save();
    else if( InId == "Edit.Undo" ) m_undo.Undo();
    else if( InId == "Edit.Redo" ) m_undo.Redo();
}


void TreeDocument::RequestFocus()
{
    m_focusRequested = true;
}


bool TreeDocument::ExportObj( std::string* OutError )
{
    auto fail = [OutError]( const std::string& InError ) {
        if( OutError ) *OutError = InError;
        return false;
    };
    MitchTree::TreeRecipe recipe = m_recipe;
    recipe.Sanitize();
    const MitchTree::TreeMesh mesh = MitchTree::Generate( recipe );
    const std::filesystem::path folder = std::filesystem::path( m_fullPath ).parent_path() / ( m_title + ".export" );
    std::error_code code;
    std::filesystem::create_directories( folder, code );
    if( code ) return fail( "Can't create " + folder.string() );

    // Generated maps as PNGs beside the model; authored maps by their own paths.
    std::string error;
    std::map<std::string, std::string> maps;
    if( recipe.BarkTextures.Generate )
    {
        const MitchTree::BarkImages bark = MitchTree::GenerateBark( recipe.BarkTextures );
        if( !MitchTree::WritePng( bark.Color, ( folder / ( m_title + "_bark_color.png" ) ).string(), error ) ) return fail( error );
        if( !MitchTree::WritePng( bark.Normal, ( folder / ( m_title + "_bark_normal.png" ) ).string(), error ) ) return fail( error );
        maps["Bark"] = m_title + "_bark_color.png";
        maps["BarkNormal"] = m_title + "_bark_normal.png";
    }
    else if( !recipe.Bark.BaseColorMap.empty() ) maps["Bark"] = recipe.Bark.BaseColorMap;
    if( recipe.LeafTextures.Generate )
    {
        const MitchTree::LeafImages leaves = MitchTree::GenerateLeaves( recipe.LeafTextures );
        if( !MitchTree::WritePng( leaves.Color, ( folder / ( m_title + "_leaf_color.png" ) ).string(), error ) ) return fail( error );
        if( !MitchTree::WritePng( leaves.Normal, ( folder / ( m_title + "_leaf_normal.png" ) ).string(), error ) ) return fail( error );
        maps["Leaves"] = m_title + "_leaf_color.png";
        maps["LeavesNormal"] = m_title + "_leaf_normal.png";
    }
    else if( !recipe.Foliage.BaseColorMap.empty() ) maps["Leaves"] = recipe.Foliage.BaseColorMap;

    std::ofstream mtl( folder / ( m_title + ".mtl" ) );
    for( const MitchTree::TreePart& part : mesh.Parts )
    {
        mtl << "newmtl " << part.Name << "\nKd 1 1 1\n";
        if( maps.count( part.Name ) ) mtl << "map_Kd " << maps[part.Name] << "\n";
        if( maps.count( part.Name + "Normal" ) ) mtl << "map_Bump " << maps[part.Name + "Normal"] << "\n";
        if( part.Name == "Leaves" && maps.count( "Leaves" ) ) mtl << "map_d " << maps["Leaves"] << "\n";
        mtl << "\n";
    }
    std::ofstream obj( folder / ( m_title + ".obj" ) );
    if( !obj || !mtl ) return fail( "Can't write the OBJ" );
    obj << "# MitchTree " << m_path << " (full detail)\nmtllib " << m_title << ".mtl\n";
    size_t base = 1;
    for( const MitchTree::TreePart& part : mesh.Parts )
    {
        obj << "o " << part.Name << "\nusemtl " << part.Name << "\n";
        for( const MitchTree::TreeVertex& vertex : part.Vertices ) obj << "v " << vertex.Position.x << ' ' << vertex.Position.y << ' ' << vertex.Position.z << '\n';
        // OBJ's v runs up the image; the engine's down.
        for( const MitchTree::TreeVertex& vertex : part.Vertices ) obj << "vt " << vertex.UV.x << ' ' << 1.f - vertex.UV.y << '\n';
        for( const MitchTree::TreeVertex& vertex : part.Vertices ) obj << "vn " << vertex.Normal.x << ' ' << vertex.Normal.y << ' ' << vertex.Normal.z << '\n';
        const std::vector<uint32_t>& indices = part.Lods.front();
        for( size_t i = 0; i + 2 < indices.size(); i += 3 )
        {
            if( indices[i] == indices[i + 1] || indices[i + 1] == indices[i + 2] ) continue;
            obj << 'f';
            for( size_t k = 0; k < 3; ++k )
            {
                const size_t index = base + indices[i + k];
                obj << ' ' << index << '/' << index << '/' << index;
            }
            obj << '\n';
        }
        base += part.Vertices.size();
    }
    m_status = "Exported " + ( folder / ( m_title + ".obj" ) ).string();
    return true;
}


bool TreeDocument::Draw()
{
    if( m_job.valid() && m_job.wait_for( std::chrono::seconds( 0 ) ) == std::future_status::ready )
    {
        FinishJob();
    }
    if( m_regenerate && !m_job.valid() )
    {
        StartJob();
    }

    const std::string windowName = "MitchTree: " + m_title + ( IsDirty() ? " *" : "" ) + "###MitchTree:" + m_path;
    if( m_focusRequested )
    {
        // Open next to the scene view the first time, unless its panel is too small for the tree
        // (then float over the editor).
        ImGuiWindow* world = ImGui::FindWindowByName( "World View" );
        if( world && world->DockId && world->Size.x >= ImGuiTheme::Em( 40.f ) && world->Size.y >= ImGuiTheme::Em( 22.f ) )
        {
            ImGui::SetNextWindowDockID( world->DockId, ImGuiCond_FirstUseEver );
        }
        else
        {
            const ImGuiViewport* viewport = ImGui::GetMainViewport();
            const ImVec2 size( std::min( ImGuiTheme::Em( 71.f ), viewport->WorkSize.x * 0.92f ), std::min( ImGuiTheme::Em( 42.f ), viewport->WorkSize.y * 0.9f ) );
            ImGui::SetNextWindowPos( ImVec2( viewport->WorkPos.x + ( viewport->WorkSize.x - size.x ) * 0.5f, viewport->WorkPos.y + ( viewport->WorkSize.y - size.y ) * 0.5f ), ImGuiCond_FirstUseEver );
            ImGui::SetNextWindowSize( size, ImGuiCond_FirstUseEver );
        }
        ImGui::SetNextWindowFocus();
        m_focusRequested = false;
    }
    const float padding = std::round( ImGuiTheme::PanelInset() * 0.5f );
    ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( padding, padding ) );
    bool open = true;
    const bool visible = ImGui::Begin( windowName.c_str(), &open, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ( IsDirty() ? ImGuiWindowFlags_UnsavedDocument : 0 ) );
    ImGui::PopStyleVar();
    m_isFocused = ImGui::IsWindowFocused( ImGuiFocusedFlags_RootAndChildWindows );
    if( !open )
    {
        if( IsDirty() ) m_showClosePrompt = true;
        else m_isOpen = false;
    }
    if( visible )
    {
        DrawToolbar();
        if( m_sidePanelWidth <= 0.f ) m_sidePanelWidth = ImGuiTheme::Em( 24.f );
        ImGui::BeginChild( "TreeSidePanel", ImVec2( m_sidePanelWidth, 0.f ), true );
        DrawSections();
        ImGui::Separator();
        DrawProperties();
        ImGui::EndChild();
        ImGui::SameLine( 0.f, 0.f );
        ImGui::InvisibleButton( "Splitter", ImVec2( ImGui::GetStyle().DockingSeparatorSize, std::max( ImGui::GetContentRegionAvail().y, 1.f ) ) );
        if( ImGui::IsItemActive() )
        {
            m_sidePanelWidth = std::clamp( m_sidePanelWidth + ImGui::GetIO().MouseDelta.x, ImGuiTheme::Em( 14.f ), ImGuiTheme::Em( 48.f ) );
        }
        if( ImGui::IsItemHovered() || ImGui::IsItemActive() ) ImGui::SetMouseCursor( ImGuiMouseCursor_ResizeEW );
        ImGui::SameLine( 0.f, 0.f );
        DrawPreviewPane();
    }
    else
    {
        m_preview->Hide();
    }
    ImGui::End();
    DrawClosePrompt();
    if( !m_isOpen ) m_preview->Hide();
    return m_isOpen;
}


void TreeDocument::DrawToolbar()
{
    if( ImGui::Button( "Save" ) ) Save();
    ImGui::SameLine();
    ImGui::BeginDisabled( !m_undo.CanUndo() );
    if( ImGui::Button( "Undo" ) ) m_undo.Undo();
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled( !m_undo.CanRedo() );
    if( ImGui::Button( "Redo" ) ) m_undo.Redo();
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::SetNextItemWidth( ImGuiTheme::Em( 8.f ) );
    if( ImGui::BeginCombo( "##Preset", "Preset", ImGuiComboFlags_HeightLarge ) )
    {
        for( const std::string& preset : MitchTree::PresetNames() )
        {
            if( ImGui::Selectable( preset.c_str() ) ) ApplyPreset( preset );
        }
        ImGui::EndCombo();
    }
    if( ImGui::IsItemHovered() ) ImGui::SetTooltip( "Replace the recipe with a built-in tree (undoable)." );
    ImGui::SameLine();
    if( ImGui::Button( "New Seed" ) ) RandomizeSeed();
    if( ImGui::IsItemHovered() ) ImGui::SetTooltip( "Another tree of the same kind." );
    ImGui::SameLine();
    if( ImGui::Button( "Export OBJ" ) )
    {
        std::string error;
        if( !ExportObj( &error ) ) YIKES( "MitchTree export: " + error );
    }
    if( ImGui::IsItemHovered() ) ImGui::SetTooltip( "Full detail as OBJ + MTL + PNG textures in <Name>.export, for other tools." );
    ImGui::SameLine();
    ImGui::TextDisabled( "|" );
    ImGui::SameLine();
    if( IsGenerating() ) ImGui::TextDisabled( "generating..." );
    else if( !m_status.empty() ) ImGui::TextDisabled( "%s", m_status.c_str() );
    else ImGui::TextColored( ImGuiTheme::SuccessText(), "%.0f ms", m_statistics.Milliseconds );
}


void TreeDocument::DrawSections()
{
    // Tree, Trunk, Level 1..n, Leaves, Leaf Material, Bark, Bark Texture, Leaf Texture, Wind, LOD.
    const int levels = static_cast<int>( m_recipe.Branches.size() );
    std::vector<std::string> labels = { "Tree", "Trunk" };
    for( int level = 0; level < levels; ++level )
    {
        labels.push_back( "Branches " + std::to_string( level + 1 ) + ( m_recipe.Branches[level].Enabled ? "" : " (off)" ) );
    }
    for( const char* label : { "Leaves", "Leaf Material", "Bark", "Bark Texture", "Leaf Texture", "Wind", "Levels of Detail" } ) labels.push_back( label );
    m_section = std::clamp( m_section, 0, static_cast<int>( labels.size() ) - 1 );
    const float rows = std::min( static_cast<float>( labels.size() ), 9.f );
    if( ImGui::BeginListBox( "##TreeSections", ImVec2( -1.f, ImGui::GetTextLineHeightWithSpacing() * rows + ImGui::GetStyle().FramePadding.y * 2.f ) ) )
    {
        for( int i = 0; i < static_cast<int>( labels.size() ); ++i )
        {
            const bool branch = i >= 2 && i < 2 + levels;
            if( branch ) ImGui::Indent();
            if( ImGui::Selectable( labels[i].c_str(), m_section == i ) ) m_section = i;
            if( branch ) ImGui::Unindent();
        }
        ImGui::EndListBox();
    }
    ImGui::BeginDisabled( levels >= MitchTree::MaxBranchLevels );
    if( ImGui::Button( "Add Level" ) )
    {
        // A finer copy of the deepest level.
        MitchTree::BranchLevel level = m_recipe.Branches.empty() ? MitchTree::BranchLevel() : m_recipe.Branches.back();
        level.Length *= 0.7f;
        level.Count = std::max( 2, level.Count / 2 );
        level.SegmentLength = std::max( 0.05f, level.SegmentLength * 0.6f );
        m_recipe.Branches.push_back( level );
        Commit( "Add Branch Level", "levels" );
        m_section = 2 + levels;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled( levels == 0 );
    if( ImGui::Button( "Remove Level" ) )
    {
        m_recipe.Branches.pop_back();
        Commit( "Remove Branch Level", "levels" );
    }
    ImGui::EndDisabled();
}


void TreeDocument::DrawProperties()
{
    ImGui::BeginChild( "TreeProperties" );
    const int levels = static_cast<int>( m_recipe.Branches.size() );
    ReflectionUI::Context context;
    bool changed = false;
    std::string section;
    auto draw = [&]( const Reflection::TypeInfo& InType, void* InData, const std::string& InSection ) {
        section = InSection;
        changed = ReflectionUI::DrawType( InType, InData, context );
    };
    if( m_section == 0 )
    {
        section = "Tree";
        ImGui::SetNextItemWidth( -1.f );
        if( ImGui::InputText( "##Name", &m_recipe.Name ) ) changed = true;
        int seed = static_cast<int>( m_recipe.Seed );
        ImGui::TextUnformatted( "Seed" );
        ImGui::SameLine();
        ImGui::SetNextItemWidth( -1.f );
        if( ImGui::DragInt( "##Seed", &seed, 1.f, 1, 1000000 ) )
        {
            m_recipe.Seed = static_cast<uint32_t>( std::max( seed, 1 ) );
            changed = true;
        }
        int crown = static_cast<int>( m_recipe.Crown );
        ImGui::TextUnformatted( "Crown" );
        ImGui::SameLine();
        ImGui::SetNextItemWidth( -1.f );
        if( ImGui::Combo( "##Crown", &crown, kCrownNames, IM_ARRAYSIZE( kCrownNames ) ) )
        {
            m_recipe.Crown = static_cast<MitchTree::CrownShape>( crown );
            changed = true;
        }
        if( ImGui::IsItemHovered() ) ImGui::SetTooltip( "Envelope of the first branch level's lengths up the crown." );
        ImGui::TextUnformatted( "Occlusion" );
        ImGui::SameLine();
        ImGui::SetNextItemWidth( -1.f );
        if( ImGui::SliderFloat( "##Occlusion", &m_recipe.Occlusion, 0.f, 1.f ) ) changed = true;
        if( ImGui::IsItemHovered() ) ImGui::SetTooltip( "Strength of the baked crown self-shadowing." );
        ImGui::Separator();
        ImGui::TextDisabled( "%u stems, %u leaf cards", m_statistics.Stems, m_statistics.Leaves );
        for( size_t lod = 0; lod < m_statistics.Triangles.size(); ++lod )
        {
            ImGui::TextDisabled( "LOD %zu: %u triangles", lod, m_statistics.Triangles[lod] );
        }
    }
    else if( m_section == 1 ) draw( MitchTree::TrunkSettings::StaticType(), &m_recipe.Trunk, "Trunk" );
    else if( m_section < 2 + levels ) draw( MitchTree::BranchLevel::StaticType(), &m_recipe.Branches[m_section - 2], "Branches " + std::to_string( m_section - 1 ) );
    else
    {
        switch( m_section - 2 - levels )
        {
        case 0: draw( MitchTree::LeafSettings::StaticType(), &m_recipe.Leaves, "Leaves" ); break;
        case 1: draw( MitchTree::LeafMaterial::StaticType(), &m_recipe.Foliage, "Leaf Material" ); break;
        case 2: draw( MitchTree::BarkMaterial::StaticType(), &m_recipe.Bark, "Bark" ); break;
        case 3: draw( MitchTree::BarkTexture::StaticType(), &m_recipe.BarkTextures, "Bark Texture" ); break;
        case 4: draw( MitchTree::LeafTexture::StaticType(), &m_recipe.LeafTextures, "Leaf Texture" ); break;
        case 5: draw( MitchTree::WindSettings::StaticType(), &m_recipe.Wind, "Wind" ); break;
        default: draw( MitchTree::LodSettings::StaticType(), &m_recipe.Lod, "Levels of Detail" ); break;
        }
    }
    if( changed )
    {
        std::string fields;
        for( const std::string& field : context.ChangedFields ) fields += ( fields.empty() ? "" : "," ) + field;
        Commit( "Edit " + section + ( fields.empty() ? "" : " " + fields ), section + ":" + fields );
    }
    ImGui::EndChild();
}


void TreeDocument::DrawPreviewPane()
{
    ImGui::BeginGroup();
    ImGui::Checkbox( "Wind", &m_preview->Wind );
    ImGui::SameLine();
    ImGui::SetNextItemWidth( ImGuiTheme::Em( 6.f ) );
    const std::string lodLabel = m_preview->ForcedLod < 0 ? "Auto (" + std::to_string( m_preview->GetDrawnLod() ) + ")" : "LOD " + std::to_string( m_preview->ForcedLod );
    if( ImGui::BeginCombo( "##PreviewLod", lodLabel.c_str() ) )
    {
        if( ImGui::Selectable( "Auto", m_preview->ForcedLod < 0 ) ) m_preview->ForcedLod = -1;
        for( int lod = 0; lod <= m_recipe.Lod.Levels; ++lod )
        {
            if( ImGui::Selectable( ( "LOD " + std::to_string( lod ) ).c_str(), m_preview->ForcedLod == lod ) ) m_preview->ForcedLod = lod;
        }
        ImGui::EndCombo();
    }
    if( ImGui::IsItemHovered() ) ImGui::SetTooltip( "Level of detail: by screen size like the game, or forced." );
    ImGui::SameLine();
    ImGui::Checkbox( "Wireframe", &m_preview->Wireframe );
    ImGui::SameLine();
    ImGui::Checkbox( "Ground", &m_preview->ShowGround );
    ImGui::SameLine();
    ImGui::Checkbox( "Scene Lighting", &m_preview->SceneLighting );
    if( ImGui::IsItemHovered() ) ImGui::SetTooltip( "Light the tree with the scene camera's sky and look instead of the studio daylight." );
    if( !m_preview->SceneLighting )
    {
        ImGui::SameLine();
        ImGui::SetNextItemWidth( ImGuiTheme::Em( 6.f ) );
        ImGui::SliderFloat( "##SunAzimuth", &m_preview->SunAzimuth, -180.f, 180.f, "Sun %.0f" );
        if( ImGui::IsItemHovered() ) ImGui::SetTooltip( "The studio sun's direction (degrees around the tree)." );
        ImGui::SameLine();
        ImGui::SetNextItemWidth( ImGuiTheme::Em( 6.f ) );
        ImGui::SliderFloat( "##SunElevation", &m_preview->SunElevation, 2.f, 90.f, "Height %.0f" );
    }
    ImGui::SameLine();
    if( ImGui::Button( "Frame" ) ) m_preview->Frame();
    if( ImGui::IsItemHovered() ) ImGui::SetTooltip( "Fit the tree in view (F). Left drag orbits, middle or Shift drag pans, the wheel zooms." );
    const ImVec2 size = ImGui::GetContentRegionAvail();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    m_preview->Draw( ImVec2( std::max( size.x, 16.f ), std::max( size.y, 16.f ) ) );
    // Statistics over the image.
    const uint8_t lod = m_preview->GetDrawnLod();
    if( lod < m_statistics.Triangles.size() )
    {
        const std::string stats = std::to_string( m_statistics.Triangles[lod] ) + " triangles (LOD " + std::to_string( lod ) + ")";
        ImGui::GetWindowDrawList()->AddText( ImVec2( origin.x + ImGuiTheme::Em( 0.5f ), origin.y + size.y - ImGui::GetTextLineHeightWithSpacing() ), IM_COL32( 255, 255, 255, 200 ), stats.c_str() );
    }
    ImGui::EndGroup();
}


void TreeDocument::DrawClosePrompt()
{
    const std::string popup = "Unsaved Tree###MTClose:" + m_path;
    if( m_showClosePrompt )
    {
        ImGui::OpenPopup( popup.c_str() );
        m_showClosePrompt = false;
    }
    if( ImGui::BeginPopupModal( popup.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize ) )
    {
        ImGui::Text( "Save changes to %s?", m_title.c_str() );
        if( ImGui::Button( "Save" ) )
        {
            Save();
            m_isOpen = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if( ImGui::Button( "Don't Save" ) )
        {
            m_isOpen = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if( ImGui::Button( "Cancel" ) ) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}
#endif
