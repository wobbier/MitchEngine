#include "TreeWidget.h"

#if USING( ME_EDITOR )
#include "CLog.h"
#include "Components/Graphics/Model.h"
#include "ECS/Entity.h"
#include "Editor/EditorActions.h"
#include "Editor/Selection.h"
#include "Events/EditorEvents.h"
#include "Path.h"
#include "Procedural/Tree/TreeAsset.h"
#include "Procedural/Tree/TreePresets.h"
#include "TreeDocument.h"
#include "Types/AssetType.h"
#include "Utils/EditorConfig.h"
#include "Utils/EditorIcons.h"
#include "imgui_internal.h"
#include <cctype>
#include <filesystem>

namespace
{
    TreeWidget* s_instance = nullptr;
}


TreeWidget::TreeWidget()
{
    s_instance = this;
    Listen<OpenTreeEvent>( [this]( const OpenTreeEvent& InEvent ) {
        Open( InEvent.TreePath );
        return true;
    } );
}


TreeWidget::~TreeWidget()
{
    if( s_instance == this )
    {
        s_instance = nullptr;
    }
}


TreeWidget* TreeWidget::Get()
{
    return s_instance;
}


void TreeWidget::RequestNew( const std::string& InPreset )
{
    RequestAssetSelectionEvent evt( [InPreset]( Path InSelected ) {
        if( TreeWidget::Get() ) TreeWidget::Get()->Create( InSelected.FullPath, InPreset );
    }, AssetType::Model, true );
    evt.Fire();
}


void TreeWidget::RequestOpen()
{
    RequestAssetSelectionEvent evt( []( Path InSelected ) {
        OpenTreeEvent open;
        open.TreePath = InSelected.GetLocalPathString();
        open.Fire();
    }, AssetType::Model );
    evt.Fire();
}


std::string TreeWidget::GetTreePath( const Entity& InEntity )
{
    const Model* model = InEntity.TryGetComponent<Model>();
    if( !model ) return {};
    const std::string path = model->GetModelPath().GetLocalPathString();
    return IsTreePath( path ) ? path : std::string();
}


bool TreeWidget::IsTreePath( const std::string& InPath )
{
    static const std::string extension = ".mtree";
    return InPath.size() > extension.size() && InPath.compare( InPath.size() - extension.size(), extension.size(), extension ) == 0;
}


const char* TreeWidget::IconFor( const MitchTree::TreeRecipe& InRecipe )
{
    switch( InRecipe.Leaves.Kind )
    {
    case MitchTree::LeafKind::None: return "binary-tree";
    case MitchTree::LeafKind::Needles: return "christmas-tree";
    case MitchTree::LeafKind::Frond: return "plant-2";
    default: return "tree";
    }
}


const char* TreeWidget::PresetIcon( const std::string& InPreset )
{
    if( InPreset == "Maple" ) return "leaf-maple";
    if( InPreset == "Pine" || InPreset == "Spruce" ) return "christmas-tree";
    if( InPreset == "Palm" ) return "plant-2";
    if( InPreset == "DeadSnag" ) return "binary-tree";
    if( InPreset == "Bush" ) return "plant";
    return "tree";
}


std::string TreeWidget::PresetLabel( const std::string& InPreset )
{
    std::string label;
    for( size_t i = 0; i < InPreset.size(); ++i )
    {
        if( i > 0 && std::isupper( static_cast<unsigned char>( InPreset[i] ) ) && std::islower( static_cast<unsigned char>( InPreset[i - 1] ) ) ) label += ' ';
        label += InPreset[i];
    }
    return label;
}


void TreeWidget::Init()
{
    EditorActions& actions = EditorActions::Get();
    auto previousClaims = actions.DocumentClaims;
    auto previousExecute = actions.DocumentExecute;
    actions.DocumentClaims = [this, previousClaims]( const std::string& InId ) {
        return ( m_focused && m_focused->ClaimsAction( InId ) ) || ( previousClaims && previousClaims( InId ) );
    };
    actions.DocumentExecute = [this, previousExecute]( const std::string& InId ) {
        if( m_focused && m_focused->ClaimsAction( InId ) ) m_focused->ExecuteAction( InId );
        else if( previousExecute ) previousExecute( InId );
    };

    // File > New Tree / Open Tree... (and the command palette).
    for( const std::string& preset : MitchTree::PresetNames() )
    {
        actions.Register( { "Tree.New." + preset, "New " + PresetLabel( preset ) + " Tree (MitchTree)...", "File", 0, 0, [preset]() { RequestNew( preset ); } } );
    }
    actions.Register( { "Tree.Open", "Open Tree (MitchTree)...", "File", 0, 0, []() { RequestOpen(); } } );
    // The hierarchy's and inspector's way in: the selected entity's tree.
    actions.Register( { "Tree.EditSelected", "Edit Selected Tree in MitchTree", "Entity", 0, 0, []() {
        Entity* entity = Selection::Get().GetActive().Get();
        if( !entity || GetTreePath( *entity ).empty() ) return;
        OpenTreeEvent open;
        open.TreePath = GetTreePath( *entity );
        open.Fire();
    }, []() {
        Entity* entity = Selection::Get().GetActive().Get();
        return entity && !GetTreePath( *entity ).empty();
    } } );
}


void TreeWidget::Destroy()
{
    m_focused = nullptr;
    m_documents.clear();
}


void TreeWidget::Render()
{
    TreeDocument* focused = nullptr;
    for( auto it = m_documents.begin(); it != m_documents.end(); )
    {
        TreeDocument* document = it->get();
        if( !document->Draw() )
        {
            if( m_focused == document ) m_focused = nullptr;
            it = m_documents.erase( it );
            continue;
        }
        if( document->IsFocused() ) focused = document;
        ++it;
    }
    // Keep routing to the last focused tree while a menu or popup is in front, but let go as soon
    // as another editor window takes focus.
    ImGuiWindow* navWindow = ImGui::GetCurrentContext()->NavWindow;
    const bool transient = navWindow && ( ( navWindow->Flags & ( ImGuiWindowFlags_Popup | ImGuiWindowFlags_MenuBar ) ) || std::string( navWindow->Name ).find( "##MainMenuBar" ) != std::string::npos );
    if( focused || !transient ) m_focused = focused;
}


TreeDocument* TreeWidget::Open( const std::string& InPath )
{
    const std::string path = Path( InPath ).GetLocalPathString();
    if( TreeDocument* existing = Find( path ) )
    {
        existing->RequestFocus();
        return existing;
    }
    auto document = std::make_unique<TreeDocument>( path );
    std::string error;
    if( !document->Load( &error ) )
    {
        YIKES( "Couldn't open tree " + path + ": " + error );
        return nullptr;
    }
    EditorConfig::GetInstance().AddRecentTree( path );
    m_recentIcons[path] = IconFor( document->GetRecipe() );
    m_documents.push_back( std::move( document ) );
    return m_documents.back().get();
}


TreeDocument* TreeWidget::Create( const std::string& InPath, const std::string& InPreset )
{
    std::string path = Path( InPath ).FullPath;
    if( Path( path ).GetExtension() != std::string( "mtree" ) ) path += ".mtree";
    MitchTree::TreeRecipe recipe;
    std::string error;
    if( !MitchTree::MakePreset( InPreset, recipe ) || !MitchTree::SaveRecipe( path, recipe, error ) )
    {
        YIKES( "Couldn't create " + path + ": " + error );
        return nullptr;
    }
    return Open( Path( path ).GetLocalPathString() );
}


void TreeWidget::DrawFileMenuItems()
{
    if( ImGui::BeginMenu( "New Tree" ) )
    {
        DrawNewTreeMenu();
        ImGui::EndMenu();
    }
    EditorActions::Get().MenuItem( "Tree.Open", "Open Tree..." );
    if( ImGui::BeginMenu( "Open Recent Tree", !EditorConfig::GetInstance().RecentTrees.empty() ) )
    {
        DrawRecentItems( false );
        ImGui::EndMenu();
    }
}


void TreeWidget::DrawViewMenu()
{
    if( !ImGui::BeginMenu( "MitchTree" ) )
    {
        return;
    }
    ImGui::SeparatorText( "Open" );
    if( m_documents.empty() ) EditorIcons::MenuItem( "tree", "No trees open", nullptr, false, false );
    for( const auto& document : m_documents )
    {
        const std::string name = std::filesystem::path( document->GetPath() ).stem().string();
        ImGui::PushID( document->GetPath().c_str() );
        if( EditorIcons::MenuItem( IconFor( document->GetRecipe() ), name.c_str(), document->IsDirty() ? "unsaved" : nullptr, document.get() == m_focused ) )
        {
            document->RequestFocus();
        }
        ImGui::SetItemTooltip( "%s", document->GetPath().c_str() );
        ImGui::PopID();
    }
    bool anyRecent = false;
    for( const std::string& recent : EditorConfig::GetInstance().RecentTrees ) anyRecent = anyRecent || !Find( recent );
    if( anyRecent )
    {
        ImGui::SeparatorText( "Recent" );
        DrawRecentItems( true );
    }
    ImGui::Separator();
    if( EditorIcons::BeginMenu( "seedling", "New Tree" ) )
    {
        DrawNewTreeMenu();
        ImGui::EndMenu();
    }
    if( EditorIcons::MenuItem( "folder-open", "Open Tree..." ) ) RequestOpen();
    const bool canEdit = EditorActions::Get().IsEnabled( "Tree.EditSelected" );
    if( EditorIcons::MenuItem( "pointer", "Edit Selected Tree", nullptr, false, canEdit ) ) EditorActions::Get().Execute( "Tree.EditSelected" );
    if( EditorIcons::MenuItem( "trees", "Trees Workspace" ) ) EditorActions::Get().Execute( "View.TreesWorkspace" );
    ImGui::SetItemTooltip( "The Terrain panel's list of the project's trees." );
    ImGui::EndMenu();
}


void TreeWidget::DrawNewTreeMenu()
{
    for( const std::string& preset : MitchTree::PresetNames() )
    {
        if( EditorIcons::MenuItem( PresetIcon( preset ), ( PresetLabel( preset ) + "..." ).c_str() ) ) RequestNew( preset );
    }
}


void TreeWidget::DrawRecentItems( bool InSkipOpen )
{
    // Copied: opening one reorders the list.
    const std::vector<std::string> recent = EditorConfig::GetInstance().RecentTrees;
    for( const std::string& path : recent )
    {
        if( InSkipOpen && Find( path ) ) continue;
        // The name, then its folder in the shortcut column.
        const std::filesystem::path file( path );
        const bool exists = Path( path ).Exists;
        ImGui::PushID( path.c_str() );
        if( EditorIcons::MenuItem( exists ? RecentIcon( path ) : "alert-triangle", file.stem().string().c_str(), file.parent_path().string().c_str(), false, exists ) )
        {
            OpenTreeEvent open;
            open.TreePath = path;
            open.Fire();
        }
        ImGui::PopID();
    }
}


const char* TreeWidget::RecentIcon( const std::string& InPath )
{
    auto found = m_recentIcons.find( InPath );
    if( found != m_recentIcons.end() ) return found->second;
    MitchTree::TreeRecipe recipe;
    std::string error;
    const char* icon = MitchTree::LoadRecipe( Path( InPath ).FullPath, recipe, error ) ? IconFor( recipe ) : "alert-triangle";
    m_recentIcons.emplace( InPath, icon );
    return icon;
}


TreeDocument* TreeWidget::Find( const std::string& InPath ) const
{
    const std::string path = Path( InPath ).GetLocalPathString();
    for( const auto& document : m_documents )
    {
        if( document->GetPath() == path ) return document.get();
    }
    return nullptr;
}


TreeDocument* TreeWidget::GetActive() const
{
    if( m_focused ) return m_focused;
    return m_documents.empty() ? nullptr : m_documents.back().get();
}


bool TreeWidget::Close( const std::string& InPath )
{
    const std::string path = Path( InPath ).GetLocalPathString();
    for( auto it = m_documents.begin(); it != m_documents.end(); ++it )
    {
        if( ( *it )->GetPath() == path )
        {
            if( m_focused == it->get() ) m_focused = nullptr;
            m_documents.erase( it );
            return true;
        }
    }
    return false;
}
#endif
