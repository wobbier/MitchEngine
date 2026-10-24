#include "PCH.h"
#include "Cores/UI/RmlUiCore.h"

#include "CLog.h"
#include "Components/Camera.h"
#include "Components/UI/UIDocument.h"
#include "Core/UpdateContext.h"
#include "Renderer.h"
#include "RenderViews.h"

#if USING( ME_RMLUI )
#include "UI/Rml/RmlInterfaces.h"
#include "UI/Rml/RmlRenderInterface.h"
#include <RmlUi/Core.h>

namespace
{
    // Interfaces must outlive every Rml::Initialise/Shutdown pair, so they live for the process.
    RmlSystemInterface s_systemInterface;
    RmlFileInterface s_fileInterface;
    RmlRenderInterface s_renderInterface;
    int s_rmlUsers = 0;
    int s_contextCounter = 0;
}
#endif

RmlUiCore::RmlUiCore( BGFXRenderer* InRenderer )
    : Base( ComponentFilter().Requires<UIDocument>() )
{
    SetIsSerializable( false );
#if USING( ME_RMLUI )
    m_renderer = InRenderer;
#endif
}


RmlUiCore::~RmlUiCore()
{
    ReleaseRml();
}


void RmlUiCore::ReleaseRml()
{
#if USING( ME_RMLUI )
    if( !m_rmlInitialized )
    {
        return;
    }
    m_rmlInitialized = false;
    if( m_context != nullptr )
    {
        Rml::RemoveContext( m_contextName );
        m_context = nullptr;
    }
    if( --s_rmlUsers == 0 )
    {
        Rml::Shutdown();
        // The renderer is still alive here (Engine::Shutdown order): release GPU handles now.
        s_renderInterface.Shutdown();
    }
#endif
}


void RmlUiCore::Init()
{
#if USING( ME_RMLUI )
    // Init runs once per AddCore, and the same core instance joins each new world.
    if( m_rmlInitialized )
    {
        return;
    }
    m_rmlInitialized = true;
    if( s_rmlUsers++ == 0 )
    {
        Rml::SetSystemInterface( &s_systemInterface );
        Rml::SetFileInterface( &s_fileInterface );
        Rml::SetRenderInterface( &s_renderInterface );
        if( m_renderer != nullptr )
        {
            s_renderInterface.Init();
        }
        Rml::Initialise();
        // The engine font; a fallback face so plain documents always have glyphs.
        if( !Rml::LoadFontFace( "Assets/Fonts/Inter-Regular.ttf", true ) )
        {
            YIKES( "[UI] Failed to load the UI font." );
        }
    }
#endif
}


#if USING( ME_RMLUI )
void RmlUiCore::EnsureContext( const Vector2& InSize )
{
    const Rml::Vector2i size( static_cast<int>( InSize.x ), static_cast<int>( InSize.y ) );
    if( m_context == nullptr )
    {
        m_contextName = "world" + std::to_string( s_contextCounter++ );
        m_context = Rml::CreateContext( m_contextName, size );
        m_contextSize = InSize;
        return;
    }
    if( m_contextSize != InSize )
    {
        m_context->SetDimensions( size );
        m_contextSize = InSize;
    }
}


void RmlUiCore::LoadDocument( UIDocument& InDocument )
{
    if( InDocument.m_document != nullptr || !InDocument.DocumentPath.Exists )
    {
        return;
    }
    Rml::ElementDocument* document = m_context->LoadDocument( InDocument.DocumentPath.GetLocalPathString() );
    if( document == nullptr )
    {
        YIKES( "[UI] Failed to load document: " + InDocument.DocumentPath.GetLocalPathString() );
        return;
    }
    InDocument.m_document = document;
    if( InDocument.Visible )
    {
        document->Show();
    }
}


void RmlUiCore::CloseDocument( UIDocument& InDocument )
{
    if( InDocument.m_document != nullptr )
    {
        static_cast<Rml::ElementDocument*>( InDocument.m_document )->Close();
        InDocument.m_document = nullptr;
    }
}
#endif


void RmlUiCore::OnEntityAdded( Entity& NewEntity )
{
    // Documents load lazily in Update: the context needs a sized output first.
}


void RmlUiCore::OnEntityRemoved( Entity& InEntity )
{
#if USING( ME_RMLUI )
    CloseDocument( InEntity.GetComponent<UIDocument>() );
#endif
}


void RmlUiCore::Update( const UpdateContext& inUpdateContext )
{
#if USING( ME_RMLUI )
    if( m_renderer == nullptr || Camera::CurrentCamera == nullptr )
    {
        return;
    }
    const Vector2 outputSize = Camera::CurrentCamera->OutputSize;
    if( outputSize.x < 1.f || outputSize.y < 1.f )
    {
        return;
    }

    s_systemInterface.AdvanceTime( inUpdateContext.GetUnscaledDeltaTime() );
    EnsureContext( outputSize );

    bool anyVisible = false;
    for( Entity& entity : GetEntities() )
    {
        UIDocument& document = entity.GetComponent<UIDocument>();
        LoadDocument( document );
        if( document.m_document != nullptr )
        {
            Rml::ElementDocument* loaded = static_cast<Rml::ElementDocument*>( document.m_document );
            if( document.Visible != loaded->IsVisible() )
            {
                document.Visible ? loaded->Show() : loaded->Hide();
            }
            anyVisible |= document.Visible;
        }
    }

    m_context->Update();

#if !USING( ME_EDITOR )
    // The spike draws over the composited backbuffer; the editor waits for the UI-2
    // composite path (its game view lives in an ImGui texture, not the backbuffer).
    if( anyVisible )
    {
        s_renderInterface.BeginFrame( Moonlight::RenderView::RmlUi,
            static_cast<uint16_t>( outputSize.x ), static_cast<uint16_t>( outputSize.y ) );
        m_context->Render();
    }
#endif
#endif
}
