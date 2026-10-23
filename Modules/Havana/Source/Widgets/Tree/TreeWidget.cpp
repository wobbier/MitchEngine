#include "TreeWidget.h"

#if USING( ME_EDITOR )
#include "CLog.h"
#include "Editor/EditorActions.h"
#include "Events/EditorEvents.h"
#include "Path.h"
#include "Procedural/Tree/TreeAsset.h"
#include "Procedural/Tree/TreePresets.h"
#include "TreeDocument.h"
#include "Types/AssetType.h"
#include "imgui_internal.h"

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

    // File > Open Tree... / New Tree (and the command palette).
    actions.Register( { "Tree.Open", "Open Tree (MitchTree)...", "File", 0, 0, []() {
        RequestAssetSelectionEvent evt( []( Path InSelected ) {
            OpenTreeEvent open;
            open.TreePath = InSelected.GetLocalPathString();
            open.Fire();
        }, AssetType::Model );
        evt.Fire();
    } } );
    // A new tree starts as an oak; the editor's Preset menu picks another kind.
    actions.Register( { "Tree.New", "New Tree (MitchTree)...", "File", 0, 0, []() {
        RequestAssetSelectionEvent evt( []( Path InSelected ) {
            std::string path = InSelected.FullPath;
            if( Path( path ).GetExtension() != std::string( "mtree" ) ) path += ".mtree";
            MitchTree::TreeRecipe recipe;
            std::string error;
            if( !MitchTree::MakePreset( "Oak", recipe ) || !MitchTree::SaveRecipe( path, recipe, error ) )
            {
                YIKES( "Couldn't create " + path + ": " + error );
                return;
            }
            OpenTreeEvent open;
            open.TreePath = Path( path ).GetLocalPathString();
            open.Fire();
        }, AssetType::Model, true );
        evt.Fire();
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
    m_documents.push_back( std::move( document ) );
    return m_documents.back().get();
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
