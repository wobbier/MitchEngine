#include "TreeDocument.h"

#if USING( ME_EDITOR )
#include "TreePreview.h"
#include "TreeWidget.h"
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
#include "Utils/EditorIcons.h"
#include "Utils/HavanaUtils.h"
#include "imgui_internal.h"
#include "misc/cpp/imgui_stdlib.h"
#include <bimg/bimg.h>
#include <algorithm>
#include <cfloat>
#include <chrono>
#include <cstdio>
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

    // The side panel's pages, by TreeDocument::Section.
    struct SectionInfo
    {
        const char* Icon;
        const char* Name;
        const char* Description;
    };
    const SectionInfo kSections[] = {
        { "tree", "Tree", "The whole tree: name, seed, crown shape and baked crown shadowing." },
        { "wood", "Trunk", "The main stem: height, girth, taper, root flare, lean and forks." },
        { "git-fork", "Branches", "A level of stems grown along the level above." },
        { "leaf", "Leaves", "Leaf cards on the outermost stems: kind, size, count, angles and crown regions." },
        { "palette", "Leaf Material", "How the leaves shade: maps, roughness, back-lit translucency and alpha cutoff." },
        { "leaf-maple", "Leaf Texture", "The generated leaf atlas: leaf shape, lobes, veins and colours." },
        { "sphere", "Bark Material", "How the bark shades: maps, roughness, normal strength and tiling." },
        { "texture", "Bark Texture", "The generated bark maps: pattern, scale, relief and moss." },
        { "wind", "Wind", "How strongly the trunk, branches and leaves move in a WindZone." },
        { "stack-2", "Levels of Detail", "The simplified levels and the screen sizes they switch at." },
    };


    std::string LevelDescription( int InLevel )
    {
        return ( InLevel == 0 ? std::string( "Limbs grown along the trunk" ) : "Stems grown along branch level " + std::to_string( InLevel ) ) + ": how many, where, at what angle and how long.";
    }


    // 950, 12.3k, 450k, 1.25M
    std::string Count( uint32_t InValue )
    {
        char text[32];
        if( InValue < 1000 ) std::snprintf( text, sizeof( text ), "%u", InValue );
        else if( InValue < 100000 ) std::snprintf( text, sizeof( text ), "%.1fk", InValue / 1000.0 );
        else if( InValue < 1000000 ) std::snprintf( text, sizeof( text ), "%.0fk", InValue / 1000.0 );
        else std::snprintf( text, sizeof( text ), "%.2fM", InValue / 1000000.0 );
        return text;
    }


    // 12,345
    std::string Grouped( uint32_t InValue )
    {
        std::string digits = std::to_string( InValue );
        for( int i = static_cast<int>( digits.size() ) - 3; i > 0; i -= 3 ) digits.insert( static_cast<size_t>( i ), "," );
        return digits;
    }


    // A thin upright rule between groups of toolbar buttons.
    void ToolbarDivider()
    {
        const float spacing = ImGuiTheme::Em( 0.6f );
        ImGui::SameLine( 0.f, spacing );
        const ImVec2 at = ImGui::GetCursorScreenPos();
        const float height = ImGui::GetFrameHeight();
        ImGui::GetWindowDrawList()->AddLine( ImVec2( std::round( at.x ) + 0.5f, at.y + height * 0.2f ), ImVec2( std::round( at.x ) + 0.5f, at.y + height * 0.8f ), ImGui::GetColorU32( ImGuiCol_Separator ) );
        ImGui::Dummy( ImVec2( 1.f, height ) );
        ImGui::SameLine( 0.f, spacing );
    }


    // An icon centred on a field-high row, before a control.
    void InlineIcon( const char* InIcon )
    {
        const ImVec2 at = ImGui::GetCursorScreenPos();
        const float line = ImGui::GetTextLineHeight();
        EditorIcons::Draw( ImGui::GetWindowDrawList(), InIcon, ImVec2( at.x, at.y + ( ImGui::GetFrameHeight() - line ) * 0.5f ), line, ImGui::GetColorU32( ImGuiCol_TextDisabled ) );
        ImGui::Dummy( ImVec2( line, ImGui::GetFrameHeight() ) );
    }


    // Toolbar read-outs: an icon and a value (not a control), explained on hover.
    float ChipWidth( const std::string& InText )
    {
        return ImGui::GetTextLineHeight() + ImGuiTheme::InnerGap() + ImGui::CalcTextSize( InText.c_str() ).x;
    }


    void DrawChip( const char* InIcon, const std::string& InText, ImU32 InColor, const std::string& InTooltip )
    {
        const float line = ImGui::GetTextLineHeight();
        const ImVec2 at = ImGui::GetCursorScreenPos();
        const float y = at.y + std::round( ( ImGui::GetFrameHeight() - line ) * 0.5f );
        ImDrawList* draw = ImGui::GetWindowDrawList();
        EditorIcons::Draw( draw, InIcon, ImVec2( at.x, y ), line, InColor );
        draw->AddText( ImVec2( at.x + line + ImGuiTheme::InnerGap(), y ), InColor, InText.c_str() );
        ImGui::Dummy( ImVec2( ChipWidth( InText ), ImGui::GetFrameHeight() ) );
        if( !InTooltip.empty() && ImGui::IsItemHovered() ) ImGui::SetTooltip( "%s", InTooltip.c_str() );
    }


    // A menu row with an icon before its label.
    bool IconSelectable( const char* InIcon, const std::string& InLabel )
    {
        const ImVec2 at = ImGui::GetCursorScreenPos();
        const float line = ImGui::GetTextLineHeight();
        const bool picked = ImGui::Selectable( ( "##" + InLabel ).c_str(), false, 0, ImVec2( ImGuiTheme::Em( 9.f ), 0.f ) );
        ImDrawList* draw = ImGui::GetWindowDrawList();
        EditorIcons::Draw( draw, InIcon, at, line, ImGui::GetColorU32( ImGuiCol_Text ) );
        draw->AddText( ImVec2( at.x + line + ImGuiTheme::InnerGap() * 1.5f, at.y ), ImGui::GetColorU32( ImGuiCol_Text ), InLabel.c_str() );
        return picked;
    }


    // A label on a dark pill over the preview image, hugging the corner at InAnchor (InRight and
    // InBottom say which corner of the pill that is).
    void OverlayPill( ImDrawList* InDraw, const ImVec2& InAnchor, bool InRight, bool InBottom, const char* InIcon, const std::string& InText )
    {
        const float line = ImGui::GetTextLineHeight();
        const float pad = ImGuiTheme::Em( 0.35f );
        const float gap = ImGuiTheme::InnerGap();
        const ImVec2 size( pad * 3.f + line + gap + ImGui::CalcTextSize( InText.c_str() ).x, line + pad * 2.f );
        const ImVec2 min( std::round( InRight ? InAnchor.x - size.x : InAnchor.x ), std::round( InBottom ? InAnchor.y - size.y : InAnchor.y ) );
        const ImU32 color = IM_COL32( 255, 255, 255, 230 );
        InDraw->AddRectFilled( min, ImVec2( min.x + size.x, min.y + size.y ), IM_COL32( 0, 0, 0, 150 ), size.y * 0.5f );
        EditorIcons::Draw( InDraw, InIcon, ImVec2( min.x + pad * 1.5f, min.y + pad ), line, color );
        InDraw->AddText( ImVec2( min.x + pad * 1.5f + line + gap, min.y + pad ), color, InText.c_str() );
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


void TreeDocument::AddLevel()
{
    const int levels = static_cast<int>( m_recipe.Branches.size() );
    if( levels >= MitchTree::MaxBranchLevels ) return;
    // A finer copy of the deepest level.
    MitchTree::BranchLevel level = m_recipe.Branches.empty() ? MitchTree::BranchLevel() : m_recipe.Branches.back();
    level.Length *= 0.7f;
    level.Count = std::max( 2, level.Count / 2 );
    level.SegmentLength = std::max( 0.05f, level.SegmentLength * 0.6f );
    m_recipe.Branches.push_back( level );
    Commit( "Add Branch Level", "levels" );
    m_section = Section::Branch;
    m_level = levels;
}


void TreeDocument::RemoveLevel( int InLevel )
{
    if( InLevel < 0 || InLevel >= static_cast<int>( m_recipe.Branches.size() ) ) return;
    m_recipe.Branches.erase( m_recipe.Branches.begin() + InLevel );
    Commit( "Remove Branch Level " + std::to_string( InLevel + 1 ), "levels" );
    if( m_recipe.Branches.empty() ) m_section = Section::Trunk;
    else m_level = std::min( InLevel, static_cast<int>( m_recipe.Branches.size() ) - 1 );
}


void TreeDocument::ToggleLevel( int InLevel )
{
    if( InLevel < 0 || InLevel >= static_cast<int>( m_recipe.Branches.size() ) ) return;
    MitchTree::BranchLevel& level = m_recipe.Branches[InLevel];
    level.Enabled = !level.Enabled;
    Commit( std::string( level.Enabled ? "Switch On" : "Switch Off" ) + " Branches " + std::to_string( InLevel + 1 ), "enable" );
}


bool TreeDocument::ShowPage( const std::string& InName, int InLevel )
{
    for( int i = 0; i < static_cast<int>( std::size( kSections ) ); ++i )
    {
        if( InName != kSections[i].Name ) continue;
        const Section section = static_cast<Section>( i );
        if( section == Section::Branch )
        {
            if( InLevel < 0 || InLevel >= static_cast<int>( m_recipe.Branches.size() ) ) return false;
            m_level = InLevel;
        }
        m_section = section;
        return true;
    }
    return false;
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
    m_status = "Exported " + m_title + ".obj";
    CLog::Log( CLog::LogType::Info, "MitchTree exported " + ( folder / ( m_title + ".obj" ) ).string() );
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
        // Undo can take away the level on show.
        const int levels = static_cast<int>( m_recipe.Branches.size() );
        if( m_section == Section::Branch && levels == 0 ) m_section = Section::Trunk;
        m_level = std::clamp( m_level, 0, std::max( levels - 1, 0 ) );

        DrawToolbar();
        // The rail names its pages when the window has room for them beside the preview.
        const bool labels = ImGui::GetContentRegionAvail().x >= ImGuiTheme::Em( 62.f );
        if( m_sidePanelWidth <= 0.f ) m_sidePanelWidth = ImGuiTheme::Em( 21.f );
        // A thin scrollbar shows when a short window cuts the rail off.
        ImGui::PushStyleColor( ImGuiCol_ChildBg, ImGuiTheme::ActivePalette().Recessed );
        ImGui::PushStyleVar( ImGuiStyleVar_ScrollbarSize, std::max( 4.f, ImGuiTheme::Em( 0.25f ) ) );
        ImGui::BeginChild( "TreeRail", ImVec2( labels ? ImGuiTheme::Em( 10.5f ) : ImGuiTheme::Em( 2.6f ), 0.f ) );
        ImGui::PopStyleColor();
        DrawRail( labels );
        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::SameLine( 0.f, ImGuiTheme::InnerGap() );
        ImGui::BeginChild( "TreeSidePanel", ImVec2( m_sidePanelWidth, 0.f ), true );
        DrawSectionHeader();
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
    const float gap = ImGuiTheme::InnerGap();
    if( EditorIcons::Button( "device-floppy", "Save", "Save the recipe (Ctrl+S). Trees placed in the scene regenerate.", IsDirty() ) ) Save();
    ImGui::SameLine( 0.f, gap );
    const std::string undo = m_undo.CanUndo() ? "Undo " + m_undo.GetUndoName() : "Undo";
    if( EditorIcons::Button( "arrow-back-up", undo.c_str(), "Ctrl+Z", m_undo.CanUndo() ) ) m_undo.Undo();
    ImGui::SameLine( 0.f, gap );
    const std::string redo = m_undo.CanRedo() ? "Redo " + m_undo.GetRedoName() : "Redo";
    if( EditorIcons::Button( "arrow-forward-up", redo.c_str(), "Ctrl+Y", m_undo.CanRedo() ) ) m_undo.Redo();

    ToolbarDivider();
    if( EditorIcons::Button( "trees", "Presets", "Replace the recipe with a built-in tree (undoable)." ) ) ImGui::OpenPopup( "TreePresets" );
    if( ImGui::BeginPopup( "TreePresets" ) )
    {
        ImGui::TextDisabled( "Start from a preset" );
        ImGui::Separator();
        for( const std::string& preset : MitchTree::PresetNames() )
        {
            if( IconSelectable( TreeWidget::PresetIcon( preset ), TreeWidget::PresetLabel( preset ) ) ) ApplyPreset( preset );
        }
        ImGui::EndPopup();
    }
    ImGui::SameLine( 0.f, gap );
    if( EditorIcons::Button( "dice-5", "New Seed", "Another tree of the same kind." ) ) RandomizeSeed();

    ToolbarDivider();
    if( EditorIcons::Button( "file-export", "Export OBJ", "Full detail as OBJ + MTL + PNG textures in <Name>.export, for other tools." ) )
    {
        std::string error;
        if( !ExportObj( &error ) ) YIKES( "MitchTree export: " + error );
    }
    if( EditorIcons::ShowPlanned() )
    {
        ImGui::SameLine( 0.f, gap );
        EditorIcons::Planned( "photo-down", "Bake Textures", "Write the generated bark and leaf maps to PNGs beside the recipe, to paint over." );
    }

    // Generation status and the tree's size, right-aligned.
    struct Chip
    {
        const char* Icon;
        std::string Text;
        ImU32 Color;
        std::string Tooltip;
    };
    const ImU32 muted = ImGui::GetColorU32( ImGuiCol_TextDisabled );
    const ImU32 success = ImGui::GetColorU32( ImGuiTheme::SuccessText() );
    std::vector<Chip> chips;
    if( IsGenerating() ) chips.push_back( { "hourglass", "generating", muted, "The tree regenerates on a worker after each edit." } );
    else if( !m_status.empty() ) chips.push_back( { "check", m_status, success, m_status } );
    else
    {
        char time[32];
        std::snprintf( time, sizeof( time ), "%.0f ms", m_statistics.Milliseconds );
        chips.push_back( { "stopwatch", time, success, "Time to generate the tree (and bake changed textures)." } );
    }
    if( !m_statistics.Triangles.empty() ) chips.push_back( { "triangle", Count( m_statistics.Triangles.front() ), muted, std::to_string( m_statistics.Triangles.front() ) + " triangles at full detail (LOD 0)" } );
    chips.push_back( { "git-fork", Count( m_statistics.Stems ), muted, std::to_string( m_statistics.Stems ) + " stems: trunk, leaders and branches" } );
    chips.push_back( { "leaf", Count( m_statistics.Leaves ), muted, std::to_string( m_statistics.Leaves ) + " leaf cards" } );
    const float spacing = ImGuiTheme::Em( 0.9f );
    float width = spacing * static_cast<float>( chips.size() - 1 );
    for( const Chip& chip : chips ) width += ChipWidth( chip.Text );
    ImGui::SameLine( 0.f, spacing );
    const float room = ImGui::GetContentRegionAvail().x;
    if( room < width ) ImGui::NewLine();
    else ImGui::SetCursorPosX( ImGui::GetCursorPosX() + room - width );
    for( size_t i = 0; i < chips.size(); ++i )
    {
        if( i > 0 ) ImGui::SameLine( 0.f, spacing );
        DrawChip( chips[i].Icon, chips[i].Text, chips[i].Color, chips[i].Tooltip );
    }
}


void TreeDocument::DrawRail( bool InLabels )
{
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const float width = ImGui::GetContentRegionAvail().x;
    const float height = InLabels ? ImGui::GetFrameHeight() : ImGuiTheme::Em( 1.6f );
    const float icon = ImGuiTheme::Em( InLabels ? 1.f : 1.15f );
    const float inset = ImGuiTheme::Em( 0.55f );
    ImGui::PushStyleVar( ImGuiStyleVar_ItemSpacing, ImVec2( 0.f, 0.f ) );

    // One page of the rail (or an action). Dimmed pages have no effect on the tree as it's set up.
    auto row = [&]( const char* InIcon, const std::string& InLabel, const std::string& InDescription, bool InSelected, bool InEnabled, bool InDimmed, int InBadge ) {
        ImGui::PushID( InLabel.c_str() );
        ImGui::BeginDisabled( !InEnabled );
        const ImVec2 min = ImGui::GetCursorScreenPos();
        const ImVec2 max( min.x + width, min.y + height );
        const bool clicked = ImGui::InvisibleButton( "##page", ImVec2( width, height ) );
        ImU32 background = ImGui::GetColorU32( ImGuiTheme::ActivePalette().Recessed );
        if( InSelected || ImGui::IsItemHovered() )
        {
            background = ImGui::GetColorU32( InSelected ? ImGuiCol_Header : ImGuiCol_FrameBgHovered );
            draw->AddRectFilled( min, max, background );
        }
        if( InSelected )
        {
            draw->AddRectFilled( min, ImVec2( min.x + std::max( 2.f, ImGuiTheme::Em( 0.15f ) ), max.y ), ImGui::GetColorU32( ImGuiTheme::AccentText() ) );
        }
        const ImU32 color = ImGui::GetColorU32( InSelected ? ImGuiTheme::AccentText() : ImGui::GetStyleColorVec4( InDimmed ? ImGuiCol_TextDisabled : ImGuiCol_Text ) );
        const ImVec2 iconMin( InLabels ? min.x + inset : min.x + ( width - icon ) * 0.5f, min.y + ( height - icon ) * 0.5f );
        EditorIcons::Draw( draw, InIcon, iconMin, icon, color );
        if( InBadge > 0 )
        {
            // The level's number on the icon's lower right, over a patch of the rail.
            const std::string badge = std::to_string( InBadge );
            const float size = std::round( ImGui::GetFontSize() * 0.68f );
            const ImVec2 at( iconMin.x + icon - size * 0.25f, iconMin.y + icon - size * 0.7f );
            const ImVec2 extent = ImGui::GetFont()->CalcTextSizeA( size, FLT_MAX, 0.f, badge.c_str() );
            draw->AddRectFilled( ImVec2( at.x - 1.f, at.y ), ImVec2( at.x + extent.x + 1.f, at.y + extent.y ), background, 2.f );
            draw->AddText( ImGui::GetFont(), size, at, color, badge.c_str() );
        }
        if( InLabels )
        {
            draw->PushClipRect( min, max, true );
            draw->AddText( ImVec2( iconMin.x + icon + ImGuiTheme::InnerGap() * 1.5f, min.y + ( height - ImGui::GetTextLineHeight() ) * 0.5f ), color, InLabel.c_str() );
            draw->PopClipRect();
        }
        ImGui::EndDisabled();
        if( ImGui::IsItemHovered( ImGuiHoveredFlags_AllowWhenDisabled ) ) EditorIcons::Tooltip( InLabel.c_str(), InDescription.c_str() );
        ImGui::PopID();
        return clicked;
    };
    // Groups: a heading beside labels, else a short rule.
    auto group = [&]( const char* InName, bool InFirst ) {
        if( InLabels )
        {
            ImGui::Dummy( ImVec2( width, ImGuiTheme::Em( InFirst ? 0.35f : 0.7f ) ) );
            ImGui::SetCursorScreenPos( ImVec2( ImGui::GetCursorScreenPos().x + inset, ImGui::GetCursorScreenPos().y ) );
            ImGui::TextDisabled( "%s", InName );
            ImGui::Dummy( ImVec2( width, ImGuiTheme::Em( 0.2f ) ) );
        }
        else if( InFirst )
        {
            ImGui::Dummy( ImVec2( width, ImGuiTheme::Em( 0.25f ) ) );
        }
        else
        {
            const ImVec2 at = ImGui::GetCursorScreenPos();
            const float y = std::round( at.y + ImGuiTheme::Em( 0.25f ) );
            draw->AddLine( ImVec2( at.x + width * 0.22f, y ), ImVec2( at.x + width * 0.78f, y ), ImGui::GetColorU32( ImGuiCol_Separator ) );
            ImGui::Dummy( ImVec2( width, ImGuiTheme::Em( 0.5f ) ) );
        }
    };
    auto page = [&]( Section InSection, const char* InUnused = nullptr ) {
        const SectionInfo& info = kSections[static_cast<int>( InSection )];
        const std::string description = InUnused ? std::string( info.Description ) + " " + InUnused : info.Description;
        if( row( info.Icon, info.Name, description, m_section == InSection, true, InUnused != nullptr, 0 ) ) m_section = InSection;
    };
    auto planned = [&]( const char* InIcon, const char* InName, const char* InDescription ) {
        if( !EditorIcons::ShowPlanned() ) return;
        row( InIcon, InName, std::string( "Planned: " ) + InDescription, false, false, false, 0 );
    };

    const int levels = static_cast<int>( m_recipe.Branches.size() );
    const bool leaves = m_recipe.Leaves.Kind != MitchTree::LeafKind::None;
    group( "STRUCTURE", true );
    page( Section::Tree );
    page( Section::Trunk );
    for( int level = 0; level < levels; ++level )
    {
        const bool enabled = m_recipe.Branches[level].Enabled;
        std::string description = LevelDescription( level );
        if( !enabled ) description += " Switched off: the tree grows without it.";
        if( row( kSections[static_cast<int>( Section::Branch )].Icon, "Branches " + std::to_string( level + 1 ), description, m_section == Section::Branch && m_level == level, true, !enabled, InLabels ? 0 : level + 1 ) )
        {
            m_section = Section::Branch;
            m_level = level;
        }
    }
    if( row( "plus", "Add Level", "A finer copy of the deepest branch level.", false, levels < MitchTree::MaxBranchLevels, true, 0 ) ) AddLevel();
    planned( "binary-tree-2", "Roots", "Surface roots that grow out of the trunk flare and follow the ground." );

    group( "FOLIAGE", false );
    page( Section::Leaves );
    const char* noLeaves = leaves ? nullptr : "(Not used: Leaves' Kind is None.)";
    page( Section::LeafMaterial, noLeaves );
    page( Section::LeafTexture, noLeaves ? noLeaves : m_recipe.LeafTextures.Generate ? nullptr : "(Not used: Generate is off, so Leaf Material's maps are used.)" );
    planned( "flower", "Flowers & Fruit", "Blossoms and fruit hung from the leaf stems." );
    planned( "temperature-sun", "Seasons", "Leaf colour, density and fall per season, blended in the scene." );

    group( "BARK", false );
    page( Section::Bark );
    page( Section::BarkTexture, m_recipe.BarkTextures.Generate ? nullptr : "(Not used: Generate is off, so Bark Material's maps are used.)" );

    group( "MOTION & DETAIL", false );
    page( Section::Wind );
    page( Section::Lod );
    planned( "cube", "Collision", "Capsule colliders for the trunk and main limbs." );
    planned( "cards", "Impostor", "A camera-facing billboard as the farthest level of detail." );
    ImGui::Dummy( ImVec2( width, ImGuiTheme::Em( 0.25f ) ) );
    ImGui::PopStyleVar();
}


void TreeDocument::DrawSectionHeader()
{
    const SectionInfo& info = kSections[static_cast<int>( m_section )];
    const bool branch = m_section == Section::Branch;
    const std::string title = branch ? "Branches " + std::to_string( m_level + 1 ) : info.Name;
    const std::string description = branch ? LevelDescription( m_level ) : info.Description;
    const bool textures = m_section == Section::LeafTexture || m_section == Section::BarkTexture;
    const int actions = branch ? 2 : textures ? 1 : 0;
    const float gap = ImGuiTheme::InnerGap();
    const float icon = ImGuiTheme::Em( 1.6f );

    const ImVec2 start = ImGui::GetCursorScreenPos();
    EditorIcons::Draw( ImGui::GetWindowDrawList(), m_section == Section::Tree ? TreeWidget::IconFor( m_recipe ) : info.Icon, ImVec2( start.x, start.y + std::round( ( ImGui::GetFrameHeight() - icon ) * 0.5f ) ), icon, ImGui::GetColorU32( ImGuiTheme::AccentText() ) );
    ImGui::SetCursorScreenPos( ImVec2( start.x + icon + gap * 2.f, start.y ) );
    ImGui::BeginGroup();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted( title.c_str() );
    if( actions > 0 )
    {
        ImGui::SameLine( 0.f, 0.f );
        const float x = HavanaUtils::RowRight() - ImGuiTheme::IconButtonsWidth( actions ) + gap;
        ImGui::SetCursorScreenPos( ImVec2( std::max( x, ImGui::GetCursorScreenPos().x + gap ), start.y ) );
        if( branch )
        {
            const bool enabled = m_recipe.Branches[m_level].Enabled;
            if( EditorIcons::Button( enabled ? "eye" : "eye-off", enabled ? "Switch Off" : "Switch On", "Grow the tree with or without this level. Its settings are kept.", true, !enabled ) ) ToggleLevel( m_level );
            ImGui::SameLine( 0.f, gap );
            if( EditorIcons::Button( "trash", "Remove Level", "Delete this branch level. The levels below it grow from the one above instead." ) ) RemoveLevel( m_level );
        }
        else
        {
            EditorIcons::Planned( "scan", "Scanned Sets", "Pick a CC0 scanned bark or leaf set instead of generating the maps." );
        }
    }
    ImGui::PushTextWrapPos( 0.f );
    ImGui::TextDisabled( "%s", description.c_str() );
    ImGui::PopTextWrapPos();
    ImGui::EndGroup();
    // Below the icon too when the text is short.
    const float bottom = std::max( ImGui::GetItemRectMax().y, start.y + icon );
    ImGui::SetCursorScreenPos( ImVec2( start.x, bottom ) );
    ImGui::Dummy( ImVec2( 0.f, ImGuiTheme::Em( 0.1f ) ) );
    ImGui::Separator();
}


void TreeDocument::DrawProperties()
{
    ImGui::BeginChild( "TreeProperties" );
    ReflectionUI::Context context;
    bool changed = false;
    std::string section = kSections[static_cast<int>( m_section )].Name;
    switch( m_section )
    {
    case Section::Tree: DrawTreeSection(); break;
    case Section::Trunk: changed = ReflectionUI::DrawType( MitchTree::TrunkSettings::StaticType(), &m_recipe.Trunk, context ); break;
    case Section::Branch:
        section = "Branches " + std::to_string( m_level + 1 );
        changed = ReflectionUI::DrawType( MitchTree::BranchLevel::StaticType(), &m_recipe.Branches[m_level], context );
        break;
    case Section::Leaves: changed = ReflectionUI::DrawType( MitchTree::LeafSettings::StaticType(), &m_recipe.Leaves, context ); break;
    case Section::LeafMaterial: changed = ReflectionUI::DrawType( MitchTree::LeafMaterial::StaticType(), &m_recipe.Foliage, context ); break;
    case Section::LeafTexture: changed = ReflectionUI::DrawType( MitchTree::LeafTexture::StaticType(), &m_recipe.LeafTextures, context ); break;
    case Section::Bark: changed = ReflectionUI::DrawType( MitchTree::BarkMaterial::StaticType(), &m_recipe.Bark, context ); break;
    case Section::BarkTexture: changed = ReflectionUI::DrawType( MitchTree::BarkTexture::StaticType(), &m_recipe.BarkTextures, context ); break;
    case Section::Wind: changed = ReflectionUI::DrawType( MitchTree::WindSettings::StaticType(), &m_recipe.Wind, context ); break;
    case Section::Lod: changed = ReflectionUI::DrawType( MitchTree::LodSettings::StaticType(), &m_recipe.Lod, context ); break;
    }
    if( changed )
    {
        std::string fields;
        for( const std::string& field : context.ChangedFields ) fields += ( fields.empty() ? "" : "," ) + field;
        Commit( "Edit " + section + ( fields.empty() ? "" : " " + fields ), section + ":" + fields );
    }
    ImGui::EndChild();
}


void TreeDocument::DrawTreeSection()
{
    const float gap = ImGuiTheme::InnerGap();
    bool changed = false;
    HavanaUtils::Label( "Name" );
    ImGui::SetNextItemWidth( -1.f );
    if( ImGui::InputText( "##Name", &m_recipe.Name ) ) changed = true;

    HavanaUtils::Label( "Seed" );
    int seed = static_cast<int>( m_recipe.Seed );
    ImGui::SetNextItemWidth( -ImGuiTheme::IconButtonsWidth( 1 ) );
    if( ImGui::DragInt( "##Seed", &seed, 1.f, 1, 1000000 ) )
    {
        m_recipe.Seed = static_cast<uint32_t>( std::max( seed, 1 ) );
        changed = true;
    }
    ImGui::SameLine( 0.f, gap );
    if( EditorIcons::Button( "dice-5", "New Seed", "Another tree of the same kind." ) ) RandomizeSeed();

    HavanaUtils::Label( "Crown" );
    int crown = static_cast<int>( m_recipe.Crown );
    ImGui::SetNextItemWidth( -1.f );
    if( ImGui::Combo( "##Crown", &crown, kCrownNames, IM_ARRAYSIZE( kCrownNames ) ) )
    {
        m_recipe.Crown = static_cast<MitchTree::CrownShape>( crown );
        changed = true;
    }
    if( ImGui::IsItemHovered() ) ImGui::SetTooltip( "Envelope of the first branch level's lengths up the crown." );

    HavanaUtils::Label( "Occlusion" );
    ImGui::SetNextItemWidth( -1.f );
    if( ImGui::SliderFloat( "##Occlusion", &m_recipe.Occlusion, 0.f, 1.f ) ) changed = true;
    if( ImGui::IsItemHovered() ) ImGui::SetTooltip( "Strength of the baked crown self-shadowing." );
    if( changed ) Commit( "Edit Tree", "Tree:" );

    // What the recipe makes.
    ImGuiTheme::SectionGap();
    ImGui::SeparatorText( "Statistics" );
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImU32 text = ImGui::GetColorU32( ImGuiCol_Text );
    const ImU32 muted = ImGui::GetColorU32( ImGuiCol_TextDisabled );
    const float line = ImGui::GetTextLineHeight();
    auto stat = [&]( const char* InIcon, const char* InName, const std::string& InValue ) {
        const ImVec2 at = ImGui::GetCursorScreenPos();
        EditorIcons::Draw( draw, InIcon, at, line, muted );
        draw->AddText( ImVec2( at.x + line + gap * 1.5f, at.y ), muted, InName );
        draw->AddText( ImVec2( HavanaUtils::ValueColumnX(), at.y ), text, InValue.c_str() );
        ImGui::Dummy( ImVec2( HavanaUtils::RowRight() - at.x, line ) );
    };
    char time[32];
    std::snprintf( time, sizeof( time ), "%.1f ms", m_statistics.Milliseconds );
    stat( "stopwatch", "Generated in", IsGenerating() ? std::string( "..." ) : std::string( time ) );
    stat( "git-fork", "Stems", Grouped( m_statistics.Stems ) );
    stat( "leaf", "Leaf cards", Grouped( m_statistics.Leaves ) );

    // Triangles per level of detail, as bars: the level the preview draws is highlighted.
    ImGuiTheme::SectionGap();
    ImGui::SeparatorText( "Triangles" );
    uint32_t most = 1;
    for( uint32_t triangles : m_statistics.Triangles ) most = std::max( most, triangles );
    const float right = HavanaUtils::RowRight();
    const float valueX = HavanaUtils::ValueColumnX();
    const float numbers = ImGui::CalcTextSize( "000.0k" ).x;
    const float rounding = ImGui::GetStyle().FrameRounding;
    for( size_t lod = 0; lod < m_statistics.Triangles.size(); ++lod )
    {
        const bool drawn = lod == m_preview->GetDrawnLod();
        const ImVec2 at = ImGui::GetCursorScreenPos();
        const ImU32 color = drawn ? text : muted;
        draw->AddText( at, color, ( "LOD " + std::to_string( lod ) ).c_str() );
        const float barMin = valueX;
        const float barMax = std::max( valueX + 1.f, right - numbers - gap * 2.f );
        const float top = std::round( at.y + line * 0.22f );
        const float bottom = std::round( at.y + line * 0.78f );
        draw->AddRectFilled( ImVec2( barMin, top ), ImVec2( barMax, bottom ), ImGui::GetColorU32( ImGuiCol_FrameBg ), rounding );
        const float fill = barMin + ( barMax - barMin ) * static_cast<float>( m_statistics.Triangles[lod] ) / static_cast<float>( most );
        draw->AddRectFilled( ImVec2( barMin, top ), ImVec2( std::max( fill, barMin + 2.f ), bottom ), drawn ? ImGui::GetColorU32( ImGuiTheme::AccentText() ) : ImGui::GetColorU32( ImGuiCol_SliderGrab ), rounding );
        const std::string value = Count( m_statistics.Triangles[lod] );
        draw->AddText( ImVec2( right - ImGui::CalcTextSize( value.c_str() ).x, at.y ), color, value.c_str() );
        ImGui::Dummy( ImVec2( right - at.x, line ) );
        if( ImGui::IsItemHovered() ) ImGui::SetTooltip( "LOD %zu: %s triangles%s", lod, Grouped( m_statistics.Triangles[lod] ).c_str(), drawn ? " (in the preview now)" : "" );
    }
}


void TreeDocument::DrawPreviewPane()
{
    ImGui::BeginGroup();
    const float gap = ImGuiTheme::InnerGap();
    const float spacing = ImGuiTheme::Em( 0.75f );
    const float button = ImGui::GetFrameHeight();
    // The controls wrap onto another row when the pane is narrow.
    auto next = []( float InWidth, float InSpacing ) {
        ImGui::SameLine( 0.f, InSpacing );
        if( ImGui::GetContentRegionAvail().x < InWidth ) ImGui::NewLine();
    };
    if( EditorIcons::Button( "wind", "Wind", "Sway the tree as a WindZone would move it.", true, m_preview->Wind ) ) m_preview->Wind = !m_preview->Wind;
    next( button, gap );
    if( EditorIcons::Button( "vector-triangle", "Wireframe", "Show the triangles.", true, m_preview->Wireframe ) ) m_preview->Wireframe = !m_preview->Wireframe;
    next( button, gap );
    if( EditorIcons::Button( "layout-bottombar", "Ground", "Show the ground disc under the tree.", true, m_preview->ShowGround ) ) m_preview->ShowGround = !m_preview->ShowGround;

    next( button * 2.f + gap, spacing );
    if( EditorIcons::Button( "bulb", "Studio Lighting", "An even daylight of the preview's own, with the sun set here.", true, !m_preview->SceneLighting ) ) m_preview->SceneLighting = false;
    next( button, gap );
    if( EditorIcons::Button( "world", "Scene Lighting", "The scene camera's sky and look (without fog): the tree as it will look where it's placed.", true, m_preview->SceneLighting ) ) m_preview->SceneLighting = true;
    if( !m_preview->SceneLighting )
    {
        const float slider = ImGuiTheme::Em( 6.f );
        next( ImGui::GetTextLineHeight() + slider * 2.f + gap * 2.f, gap );
        InlineIcon( "sun" );
        ImGui::SameLine( 0.f, gap );
        ImGui::SetNextItemWidth( slider );
        ImGui::SliderFloat( "##SunAzimuth", &m_preview->SunAzimuth, -180.f, 180.f, "Sun %.0f" );
        if( ImGui::IsItemHovered() ) ImGui::SetTooltip( "The studio sun's direction (degrees around the tree)." );
        ImGui::SameLine( 0.f, gap );
        ImGui::SetNextItemWidth( slider );
        ImGui::SliderFloat( "##SunElevation", &m_preview->SunElevation, 2.f, 90.f, "Height %.0f" );
        if( ImGui::IsItemHovered() ) ImGui::SetTooltip( "The studio sun's height above the horizon (degrees)." );
    }

    const float combo = ImGuiTheme::Em( 6.5f );
    next( ImGui::GetTextLineHeight() + gap + combo, spacing );
    InlineIcon( "stack-2" );
    ImGui::SameLine( 0.f, gap );
    ImGui::SetNextItemWidth( combo );
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

    if( EditorIcons::ShowPlanned() )
    {
        next( button * 2.f + gap, spacing );
        EditorIcons::Planned( "box-model-2", "Bounds", "Draw the tree's bounding box and pivot." );
        next( button, gap );
        EditorIcons::Planned( "bone", "Skeleton", "Overlay the stems the mesh is grown from." );
    }
    next( button, spacing );
    if( EditorIcons::Button( "focus-centered", "Frame", "Fit the tree in view (F). Left drag orbits, middle or Shift drag pans, the wheel zooms." ) ) m_preview->Frame();

    const ImVec2 available = ImGui::GetContentRegionAvail();
    const ImVec2 size( std::max( available.x, 16.f ), std::max( available.y, 16.f ) );
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    m_preview->Draw( size );
    const bool hovered = ImGui::IsItemHovered();

    // Over the image: the drawn level's size, generation, and (while hovered) the controls.
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const float margin = ImGuiTheme::Em( 0.5f );
    const uint8_t lod = m_preview->GetDrawnLod();
    if( lod < m_statistics.Triangles.size() )
    {
        OverlayPill( draw, ImVec2( origin.x + margin, origin.y + size.y - margin ), false, true, "stack-2", "LOD " + std::to_string( lod ) + "   " + Grouped( m_statistics.Triangles[lod] ) + " triangles" );
    }
    if( IsGenerating() )
    {
        OverlayPill( draw, ImVec2( origin.x + size.x - margin, origin.y + margin ), true, false, "hourglass", "Generating" );
    }
    if( hovered && size.x >= ImGuiTheme::Em( 34.f ) )
    {
        OverlayPill( draw, ImVec2( origin.x + size.x - margin, origin.y + size.y - margin ), true, true, "rotate-360", "Drag to orbit, Shift drag to pan, F frames" );
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
