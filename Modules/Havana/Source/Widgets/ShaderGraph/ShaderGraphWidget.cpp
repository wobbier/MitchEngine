#include "ShaderGraphWidget.h"

#if USING( ME_EDITOR )
#include "CLog.h"
#include "Editor/EditorActions.h"
#include "Events/EditorEvents.h"
#include "Path.h"
#include "ShaderGraphDocument.h"
#include "imgui_internal.h"

namespace
{
    ShaderGraphWidget* s_instance = nullptr;


    std::string NormalizePath( const std::string& InPath )
    {
        // Documents are keyed by project path regardless of how they were referenced.
        return Path( InPath ).GetLocalPathString();
    }
}


ShaderGraphWidget::ShaderGraphWidget()
    : HavanaWidget( "Shader Graph" )
{
    s_instance = this;
    Listen<OpenShaderGraphEvent>( [this]( const OpenShaderGraphEvent& InEvent ) {
        Open( InEvent.GraphPath );
        return true;
    } );
}


ShaderGraphWidget::~ShaderGraphWidget()
{
    if( s_instance == this )
    {
        s_instance = nullptr;
    }
}


ShaderGraphWidget* ShaderGraphWidget::Get()
{
    return s_instance;
}


void ShaderGraphWidget::Init()
{
    EditorActions& actions = EditorActions::Get();
    actions.DocumentClaims = [this]( const std::string& InId ) {
        return m_focused && m_focused->ClaimsAction( InId );
    };
    actions.DocumentExecute = [this]( const std::string& InId ) {
        if( m_focused )
        {
            m_focused->ExecuteAction( InId );
        }
    };
}


void ShaderGraphWidget::Destroy()
{
    EditorActions::Get().DocumentClaims = nullptr;
    EditorActions::Get().DocumentExecute = nullptr;
    m_focused = nullptr;
    m_documents.clear();
}


void ShaderGraphWidget::Update()
{
}


void ShaderGraphWidget::Render()
{
    ShaderGraphDocument* focused = nullptr;
    for( auto it = m_documents.begin(); it != m_documents.end(); )
    {
        ShaderGraphDocument* document = it->get();
        if( !document->Draw() )
        {
            if( m_focused == document )
            {
                m_focused = nullptr;
            }
            it = m_documents.erase( it );
            continue;
        }
        if( document->IsFocused() )
        {
            focused = document;
        }
        ++it;
    }
    // Keep routing to the last focused graph while a menu or popup is in front, but let go as
    // soon as another editor window takes focus.
    ImGuiWindow* navWindow = ImGui::GetCurrentContext()->NavWindow;
    const bool transient = navWindow && ( ( navWindow->Flags & ( ImGuiWindowFlags_Popup | ImGuiWindowFlags_MenuBar ) ) || std::string( navWindow->Name ).find( "##MainMenuBar" ) != std::string::npos );
    if( focused || !transient )
    {
        m_focused = focused;
    }
}


ShaderGraphDocument* ShaderGraphWidget::Open( const std::string& InPath )
{
    const std::string path = NormalizePath( InPath );
    if( ShaderGraphDocument* existing = Find( path ) )
    {
        existing->RequestFocus();
        return existing;
    }
    auto document = std::make_unique<ShaderGraphDocument>( path );
    std::string error;
    if( !document->Load( &error ) )
    {
        YIKES( "Couldn't open shader graph " + path + ": " + error );
        return nullptr;
    }
    m_documents.push_back( std::move( document ) );
    return m_documents.back().get();
}


ShaderGraphDocument* ShaderGraphWidget::Find( const std::string& InPath ) const
{
    const std::string path = NormalizePath( InPath );
    for( const auto& document : m_documents )
    {
        if( document->GetPath() == path )
        {
            return document.get();
        }
    }
    return nullptr;
}


ShaderGraphDocument* ShaderGraphWidget::GetActive() const
{
    if( m_focused )
    {
        return m_focused;
    }
    return m_documents.empty() ? nullptr : m_documents.back().get();
}


bool ShaderGraphWidget::Close( const std::string& InPath )
{
    const std::string path = NormalizePath( InPath );
    for( auto it = m_documents.begin(); it != m_documents.end(); ++it )
    {
        if( ( *it )->GetPath() == path )
        {
            if( m_focused == it->get() )
            {
                m_focused = nullptr;
            }
            m_documents.erase( it );
            return true;
        }
    }
    return false;
}
#endif
