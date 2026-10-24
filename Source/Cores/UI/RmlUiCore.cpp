#include "PCH.h"
#include "Cores/UI/RmlUiCore.h"

#include "CLog.h"
#include "Components/Camera.h"
#include "Components/UI/UIDocument.h"
#include "Core/UpdateContext.h"
#include "Engine/Engine.h"
#include "Engine/Input.h"
#include "Events/EditorEvents.h"
#include "Renderer.h"
#include "RenderViews.h"

#if USING( ME_RMLUI )
#include "Resource/ResourceCache.h"
#include "UI/Rml/RmlInput.h"
#include "UI/Rml/RmlInterfaces.h"
#include "UI/Rml/RmlRenderInterface.h"
#include <RmlUi/Core.h>
#include <RmlUi/Debugger.h>
#include <SDL_mouse.h>
#if USING( ME_EDITOR )
#include <imgui.h>
#endif

namespace
{
    // Interfaces must outlive every Rml::Initialise/Shutdown pair, so they live for the process.
    RmlSystemInterface s_systemInterface;
    RmlFileInterface s_fileInterface;
    RmlRenderInterface s_renderInterface;
    int s_rmlUsers = 0;
    int s_contextCounter = 0;


    // Inline event actions, a stopgap until the UI-3 MVC bindings: any element can declare
    //   <button onclick="set-text:#target|New text">...</button>
    // and flows (ui-click) and designers get scriptable state changes without code.
    class RmlActionListener final
        : public Rml::EventListener
    {
    public:
        explicit RmlActionListener( const Rml::String& InValue )
            : m_value( InValue )
        {
        }

        void ProcessEvent( Rml::Event& InEvent ) override
        {
            const size_t colon = m_value.find( ':' );
            if( colon == Rml::String::npos )
            {
                return;
            }
            const Rml::String action = m_value.substr( 0, colon );
            const Rml::String rest = m_value.substr( colon + 1 );
            if( action == "set-text" )
            {
                const size_t bar = rest.find( '|' );
                if( bar == Rml::String::npos )
                {
                    return;
                }
                const Rml::String selector = rest.substr( 0, bar );
                const Rml::String text = rest.substr( bar + 1 );
                Rml::ElementDocument* document = InEvent.GetTargetElement()->GetOwnerDocument();
                if( Rml::Element* target = document ? document->QuerySelector( selector ) : nullptr )
                {
                    target->SetInnerRML( text );
                }
            }
        }

        void OnDetach( Rml::Element* ) override
        {
            delete this;
        }

    private:
        Rml::String m_value;
    };


    class RmlActionInstancer final
        : public Rml::EventListenerInstancer
    {
    public:
        Rml::EventListener* InstanceEventListener( const Rml::String& InValue, Rml::Element* ) override
        {
            return new RmlActionListener( InValue );
        }
    };

    RmlActionInstancer s_actionInstancer;
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
    if( bgfx::isValid( m_frameBuffer ) )
    {
        bgfx::destroy( m_frameBuffer );
        m_frameBuffer = BGFX_INVALID_HANDLE;
        m_colorTexture = BGFX_INVALID_HANDLE;
        m_targetWidth = 0;
        m_targetHeight = 0;
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
        Rml::Factory::RegisterEventListenerInstancer( &s_actionInstancer );
        // The engine font; a fallback face so plain documents always have glyphs.
        if( !Rml::LoadFontFace( "Assets/Fonts/Inter-Regular.ttf", true ) )
        {
            YIKES( "[UI] Failed to load the UI font." );
        }
    }

    // .rml/.rcss edits reload live; the event fires on the main thread.
    Listen<AssetsChangedEvent>( [this]( const AssetsChangedEvent& InEvent )
    {
        for( const std::string& path : InEvent.Paths )
        {
            const size_t dot = path.rfind( '.' );
            const std::string extension = dot == std::string::npos ? std::string() : path.substr( dot );
            if( extension == ".rml" || extension == ".rcss" )
            {
                m_changedUIFiles.push_back( path );
            }
        }
    } );
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
#if !USING( ME_RETAIL )
        Rml::Debugger::Initialise( m_context );
#endif
        return;
    }
    if( m_contextSize != InSize )
    {
        m_context->SetDimensions( size );
        m_contextSize = InSize;
    }
}


void RmlUiCore::EnsureRenderTarget( uint16_t InWidth, uint16_t InHeight )
{
    if( bgfx::isValid( m_frameBuffer ) && m_targetWidth == InWidth && m_targetHeight == InHeight )
    {
        return;
    }
    if( bgfx::isValid( m_frameBuffer ) )
    {
        bgfx::destroy( m_frameBuffer );
    }
    const uint64_t colorFlags = BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
    bgfx::TextureHandle attachments[2];
    attachments[0] = bgfx::createTexture2D( InWidth, InHeight, false, 1, bgfx::TextureFormat::RGBA8, colorFlags );
    attachments[1] = bgfx::createTexture2D( InWidth, InHeight, false, 1, bgfx::TextureFormat::D24S8, BGFX_TEXTURE_RT_WRITE_ONLY );
    m_frameBuffer = bgfx::createFrameBuffer( 2, attachments, true );
    m_colorTexture = attachments[0];
    m_targetWidth = InWidth;
    m_targetHeight = InHeight;
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
    // Sort order stacks documents without touching their stylesheets.
    document->SetProperty( "z-index", std::to_string( InDocument.SortOrder ) );
    if( InDocument.Visible )
    {
        document->Show();
    }
    INFO( "UI", "UI: loaded " + InDocument.DocumentPath.GetLocalPathString() );
}


void RmlUiCore::CloseDocument( UIDocument& InDocument )
{
    if( InDocument.m_document != nullptr )
    {
        static_cast<Rml::ElementDocument*>( InDocument.m_document )->Close();
        InDocument.m_document = nullptr;
    }
}


bool RmlUiCore::WantsMouse() const
{
    return m_context != nullptr && m_context->IsMouseInteracting();
}


void RmlUiCore::ProcessInput()
{
    Input& gameInput = GetEngine().GetInput();

    int modifiers = 0;
    const bool shiftDown = gameInput.IsKeyDown( KeyCode::LeftShift ) || gameInput.IsKeyDown( KeyCode::RightShift );
    if( shiftDown )
    {
        modifiers |= Rml::Input::KM_SHIFT;
    }
    const bool ctrlDown = gameInput.IsKeyDown( KeyCode::LeftControl ) || gameInput.IsKeyDown( KeyCode::RightControl );
    if( ctrlDown )
    {
        modifiers |= Rml::Input::KM_CTRL;
    }
    const bool altDown = gameInput.IsKeyDown( KeyCode::LeftAlt ) || gameInput.IsKeyDown( KeyCode::RightAlt );
    if( altDown )
    {
        modifiers |= Rml::Input::KM_ALT;
    }

    Vector2 mousePosition = gameInput.GetMousePosition();
#if USING( ME_EDITOR )
    // The game view lives inside the editor: same mapping the Ultralight core uses.
    const ImGuiViewport* mainViewport = ImGui::GetMainViewport();
    const Vector2 windowPosition( mainViewport->Pos.x, mainViewport->Pos.y );
    const Vector2 offset = gameInput.GetMouseOffset();
    mousePosition = Vector2( ( windowPosition.x + mousePosition.x ) - offset.x,
                             ( windowPosition.y + mousePosition.y ) - offset.y );
    const bool fireMouse = !gameInput.GetMousePosition().IsZero();
#else
    const bool fireMouse = true;
#endif

    if( fireMouse )
    {
        m_context->ProcessMouseMove( static_cast<int>( mousePosition.x ), static_cast<int>( mousePosition.y ), modifiers );
        const MouseButton buttons[3] = { MouseButton::Left, MouseButton::Right, MouseButton::Middle };
        for( int index = 0; index < 3; ++index )
        {
            const bool down = gameInput.IsMouseButtonDown( buttons[index] );
            if( down && !m_mouseWasDown[index] )
            {
                m_context->ProcessMouseButtonDown( index, modifiers );
            }
            else if( !down && m_mouseWasDown[index] )
            {
                m_context->ProcessMouseButtonUp( index, modifiers );
            }
            m_mouseWasDown[index] = down;
        }
        const Vector2 scroll = gameInput.GetMouseScrollDelta();
        if( scroll.y != 0.f )
        {
            m_context->ProcessMouseWheel( Rml::Vector2f( 0.f, -scroll.y ), modifiers );
        }
    }

    for( const KeyPressEvent& keyEvent : gameInput.m_keyEventsThisFrame )
    {
        const Rml::Input::KeyIdentifier key = RmlInput::ToKeyIdentifier( keyEvent.Key );
        if( keyEvent.State == KeyState::Released )
        {
            m_context->ProcessKeyUp( key, modifiers );
            continue;
        }
#if !USING( ME_RETAIL )
        if( keyEvent.State == KeyState::Pressed && keyEvent.Key == static_cast<uint32_t>( KeyCode::F7 ) )
        {
            Rml::Debugger::SetVisible( !Rml::Debugger::IsVisible() );
            continue;
        }
#endif
        m_context->ProcessKeyDown( key, modifiers );
        if( keyEvent.State == KeyState::Pressed && !ctrlDown && !altDown )
        {
            // Synthesized text until real SDL_TEXTINPUT routing (UI-4).
            if( const char character = RmlInput::ToCharacter( keyEvent.Key, shiftDown ) )
            {
                m_context->ProcessTextInput( character );
            }
        }
    }
}


void RmlUiCore::ApplyRequestedCursor()
{
    if( GetEngine().GetWindow() == nullptr )
    {
        return;
    }
    const std::string& wanted = s_systemInterface.GetRequestedCursor();
    if( wanted == m_appliedCursor )
    {
        return;
    }
    m_appliedCursor = wanted;

    SDL_SystemCursor cursor = SDL_SYSTEM_CURSOR_ARROW;
    if( wanted == "pointer" ) { cursor = SDL_SYSTEM_CURSOR_HAND; }
    else if( wanted == "text" ) { cursor = SDL_SYSTEM_CURSOR_IBEAM; }
    else if( wanted == "move" ) { cursor = SDL_SYSTEM_CURSOR_SIZEALL; }
    else if( wanted == "resize" ) { cursor = SDL_SYSTEM_CURSOR_SIZENWSE; }
    else if( wanted == "cross" ) { cursor = SDL_SYSTEM_CURSOR_CROSSHAIR; }
    else if( wanted == "unavailable" ) { cursor = SDL_SYSTEM_CURSOR_NO; }

    static SDL_Cursor* s_cursors[SDL_NUM_SYSTEM_CURSORS] = {};
    if( s_cursors[cursor] == nullptr )
    {
        s_cursors[cursor] = SDL_CreateSystemCursor( cursor );
    }
    SDL_SetCursor( s_cursors[cursor] );
}


void RmlUiCore::HotReload()
{
    if( m_changedUIFiles.empty() )
    {
        return;
    }
    const std::vector<std::string> changed = std::move( m_changedUIFiles );
    m_changedUIFiles = {};

    bool stylesChanged = false;
    int reloadedDocuments = 0;
    for( const std::string& path : changed )
    {
        if( path.size() > 5 && path.compare( path.size() - 5, 5, ".rcss" ) == 0 )
        {
            stylesChanged = true;
            continue;
        }
        // An .rml edit reloads the documents that came from it (matched by cache identity,
        // so the watcher's absolute path meets the component's logical one).
        const std::string changedKey = ResourceCache::CacheKey( path );
        for( Entity& entity : GetEntities() )
        {
            UIDocument& document = entity.GetComponent<UIDocument>();
            if( document.m_document != nullptr && ResourceCache::CacheKey( document.DocumentPath ) == changedKey )
            {
                CloseDocument( document );
                LoadDocument( document );
                ++reloadedDocuments;
            }
        }
    }

    if( stylesChanged )
    {
        Rml::Factory::ClearStyleSheetCache();
        for( Entity& entity : GetEntities() )
        {
            UIDocument& document = entity.GetComponent<UIDocument>();
            if( document.m_document != nullptr )
            {
                static_cast<Rml::ElementDocument*>( document.m_document )->ReloadStyleSheet();
                ++reloadedDocuments;
            }
        }
    }

    if( reloadedDocuments > 0 )
    {
        INFO( "UI", "UI: reloaded " + std::to_string( reloadedDocuments ) + " document(s)" );
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
    HotReload();

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

    if( anyVisible )
    {
        ProcessInput();
        ApplyRequestedCursor();
    }

    m_context->Update();

    Moonlight::CameraData* cameraData = GetEngine().GetRenderer().GetCameraCache().Get( Camera::CurrentCamera->GetCameraId() );
    if( anyVisible )
    {
        const uint16_t width = static_cast<uint16_t>( outputSize.x );
        const uint16_t height = static_cast<uint16_t>( outputSize.y );
        EnsureRenderTarget( width, height );
        s_renderInterface.BeginFrame( Moonlight::RenderView::RmlUi, width, height, m_frameBuffer );
        m_context->Render();
        if( cameraData != nullptr )
        {
            cameraData->RmlTexture = m_colorTexture;
        }
    }
    else if( cameraData != nullptr )
    {
        cameraData->RmlTexture = BGFX_INVALID_HANDLE;
    }
#endif
}
