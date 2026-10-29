#include "PCH.h"
#include "Cores/UI/RmlUiCore.h"

#include "CLog.h"
#include "Components/Camera.h"
#include "Components/UI/UIDocument.h"
#include "Core/UpdateContext.h"
#include "Engine/Engine.h"
#include "Engine/Input.h"
#include "Events/AudioEvents.h"
#include "Events/EditorEvents.h"
#include "Events/SceneEvents.h"
#include "Renderer.h"
#include "RenderViews.h"

#if USING( ME_RMLUI )
#include "Resource/ResourceCache.h"
#include "VFS/VFS.h"
#include "UI/Rml/RmlInput.h"
#include "UI/Rml/RmlInterfaces.h"
#include "UI/Rml/RmlRenderInterface.h"
#include "UI/Rml/RmlSettings.h"
#include "Engine/LoadingScreen.h"
#include "Engine/Localization.h"
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


    // Inline event actions: menus and flows (ui-click) get common engine verbs without code.
    //   <button onclick="set-text:#target|New text">       replace an element's content
    //   <button onclick="load-scene:Assets/Main.lvl">        queue a scene load
    //   <button onclick="load-scene-async:Assets/Main.lvl">  load it under the loading screen
    //   <button onclick="play-sound:Assets/Sounds/Click.wav"> a 2D one-shot on the UI bus
    //   <button onclick="quit">                              leave the game (ignored in the editor)
    //   <button onclick="hide-document">                     hide this document (a menu's Back)
    //   <button onclick="show-document:Options">             show / toggle-document the UIDocument
    //                                                        on the entity with that name
    // Player settings verbs (setting:, rebind:, tab:...) are RmlSettings' (UI/Rml/RmlSettings.h).
    // Anything stateful belongs in a script view model (UI.Model, data-event-click).
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
            const Rml::String action = m_value.substr( 0, colon );
            const Rml::String rest = colon == Rml::String::npos ? Rml::String() : m_value.substr( colon + 1 );
            if( action == "load-scene" )
            {
                // Queued: a load tears the world (and this document) down, never mid-event.
                LoadSceneEvent evt;
                evt.Level = Path( rest ).GetLocalPath();
                evt.Queue();
                INFO( "UI", "UI: load-scene " + rest );
            }
            else if( action == "load-scene-async" )
            {
                LoadingScreen::Get().LoadSceneAsync( Path( rest ).GetLocalPathString() );
            }
            else if( action == "play-sound" )
            {
                PlayAudioEvent evt( rest );
                evt.Bus = AudioBus::UI;
                evt.Fire();
            }
            else if( action == "quit" )
            {
#if USING( ME_EDITOR )
                INFO( "UI", "UI: quit (ignored in the editor)" );
#else
                GetEngine().Quit();
#endif
            }
            else if( action == "hide-document" )
            {
                if( RmlUiCore* core = GetEngine().RmlUi )
                {
                    core->HideDocument( InEvent.GetTargetElement()->GetOwnerDocument() );
                }
            }
            else if( action == "show-document" || action == "toggle-document" )
            {
                RmlUiCore* core = GetEngine().RmlUi;
                const bool found = core && ( action == "show-document" ? core->SetDocumentVisible( rest, true ) : core->ToggleDocument( rest ) );
                if( !found )
                {
                    YIKES( "[UI] " + action + ": no UIDocument entity named '" + rest + "'" );
                }
            }
            else if( RmlSettings::HandleAction( action, rest, InEvent ) )
            {
            }
            else if( action == "set-text" )
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
    // The context owns the documents: the overlays reload into the next one.
    for( Overlay& overlay : m_overlays )
    {
        overlay.Document = nullptr;
    }
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
        // Extra coverage (CJK, symbols): every face under Assets/Fonts/Fallback joins the
        // fallback chain, game files shadowing engine ones like any asset.
        VFS::Get().Enumerate( "Assets/Fonts/Fallback", false, []( const std::string& InPath, bool InIsDirectory )
        {
            if( !InIsDirectory && Rml::LoadFontFace( InPath, true ) )
            {
                INFO( "UI", "UI: fallback font " + InPath );
            }
        } );
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
        m_context->SetDensityIndependentPixelRatio( m_textScale );
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


bool RmlUiCore::SetDocumentVisible( const std::string& InEntityName, bool InVisible )
{
    bool found = false;
    for( Entity& entity : GetEntities() )
    {
        if( entity.GetName() == InEntityName )
        {
            entity.GetComponent<UIDocument>().Visible = InVisible;
            found = true;
        }
    }
    return found;
}


std::vector<RmlUiCore::LoadedDocument> RmlUiCore::GetLoadedDocuments()
{
    std::vector<LoadedDocument> documents;
    for( Entity& entity : GetEntities() )
    {
        UIDocument& document = entity.GetComponent<UIDocument>();
        if( document.m_document != nullptr )
        {
            documents.push_back( { entity.GetName(), document.DocumentPath.GetLocalPathString(), static_cast<Rml::ElementDocument*>( document.m_document ) } );
        }
    }
    return documents;
}


bool RmlUiCore::ToggleDocument( const std::string& InEntityName )
{
    return SetDocumentVisible( InEntityName, !IsDocumentVisible( InEntityName ) );
}


bool RmlUiCore::IsDocumentVisible( const std::string& InEntityName )
{
    for( Entity& entity : GetEntities() )
    {
        if( entity.GetName() == InEntityName && entity.GetComponent<UIDocument>().Visible )
        {
            return true;
        }
    }
    return false;
}


void RmlUiCore::HideDocument( const void* InDocument )
{
    for( Entity& entity : GetEntities() )
    {
        UIDocument& document = entity.GetComponent<UIDocument>();
        if( document.m_document != nullptr && document.m_document == InDocument )
        {
            document.Visible = false;
        }
    }
}


void RmlUiCore::ShowOverlay( const std::string& InPath, int InSortOrder )
{
    if( IsOverlayShown( InPath ) )
    {
        return;
    }
    Overlay overlay;
    overlay.Path = InPath;
    overlay.SortOrder = InSortOrder;
    m_overlays.push_back( std::move( overlay ) );
}


void RmlUiCore::HideOverlay( const std::string& InPath )
{
    for( auto it = m_overlays.begin(); it != m_overlays.end(); )
    {
        if( it->Path == InPath )
        {
            if( it->Document != nullptr )
            {
                it->Document->Close();
            }
            it = m_overlays.erase( it );
        }
        else
        {
            ++it;
        }
    }
}


bool RmlUiCore::IsOverlayShown( const std::string& InPath ) const
{
    return std::any_of( m_overlays.begin(), m_overlays.end(), [&]( const Overlay& InOverlay ) { return InOverlay.Path == InPath; } );
}


void RmlUiCore::LoadOverlays()
{
    for( Overlay& overlay : m_overlays )
    {
        if( overlay.Document != nullptr || overlay.Failed )
        {
            continue;
        }
        const Path path( overlay.Path );
        overlay.Document = path.Exists ? m_context->LoadDocument( path.GetLocalPathString() ) : nullptr;
        if( overlay.Document == nullptr )
        {
            YIKES( "[UI] Failed to load overlay: " + overlay.Path );
            overlay.Failed = true;
            continue;
        }
        overlay.Document->SetProperty( "z-index", std::to_string( overlay.SortOrder ) );
        RmlSettings::OnDocumentLoaded( *overlay.Document );
        overlay.Document->Show();
        INFO( "UI", "UI: overlay " + overlay.Path );
    }
}


void RmlUiCore::CloseOverlays()
{
    for( Overlay& overlay : m_overlays )
    {
        if( overlay.Document != nullptr )
        {
            overlay.Document->Close();
            overlay.Document = nullptr;
        }
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
    // Sort order stacks documents without touching their stylesheets.
    document->SetProperty( "z-index", std::to_string( InDocument.SortOrder ) );
    RmlSettings::OnDocumentLoaded( *document );
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

    // A "press a key" rebind owns the input until the frame after it ends.
    const bool held = RmlSettings::HoldsInput();

    // Gamepad navigation: the dpad moves focus (the documents' nav properties), South activates.
    if( gameInput.IsGamepadConnected() )
    {
        constexpr GamepadButton kNavButtons[5] = { GamepadButton::DPadUp, GamepadButton::DPadDown, GamepadButton::DPadLeft, GamepadButton::DPadRight, GamepadButton::South };
        constexpr int kNavKeys[5] = { Rml::Input::KI_UP, Rml::Input::KI_DOWN, Rml::Input::KI_LEFT, Rml::Input::KI_RIGHT, Rml::Input::KI_RETURN };
        for( int i = 0; i < 5; ++i )
        {
            const bool down = gameInput.IsGamepadButtonDown( kNavButtons[i] );
            if( down && !m_gamepadNavDown[i] && !held )
            {
                InjectUiKey( kNavKeys[i] );
            }
            m_gamepadNavDown[i] = down;
        }
    }

    Vector2 mousePosition = gameInput.GetMousePosition();
#if USING( ME_EDITOR )
    // The game view lives inside the editor: map the window mouse into the game view.
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
            const bool down = gameInput.IsMouseButtonDown( buttons[index] ) && !held;
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
        if( held && keyEvent.State != KeyState::Released )
        {
            continue;
        }
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


// --- Script view models (UI-3) ------------------------------------------------------------------

void RmlUiCore::ModelEnsure( const std::string& InModel )
{
    m_models[InModel];
}


void RmlUiCore::ModelSetNumber( const std::string& InModel, const std::string& InKey, double InValue )
{
    ModelValue& value = m_models[InModel].Values[InKey];
    value.Number = InValue;
    value.IsString = false;
    value.Dirty = true;
}


void RmlUiCore::ModelSetString( const std::string& InModel, const std::string& InKey, const std::string& InValue )
{
    ModelValue& value = m_models[InModel].Values[InKey];
    value.String = InValue;
    value.IsString = true;
    value.Dirty = true;
}


void RmlUiCore::ModelRegisterEvent( const std::string& InModel, const std::string& InKey )
{
    m_models[InModel].Events.emplace( InKey, false );
}


double RmlUiCore::ModelGetNumber( const std::string& InModel, const std::string& InKey ) const
{
    const auto model = m_models.find( InModel );
    if( model == m_models.end() )
    {
        return 0.0;
    }
    const auto value = model->second.Values.find( InKey );
    return value != model->second.Values.end() && !value->second.IsString ? value->second.Number : 0.0;
}


std::string RmlUiCore::ModelGetString( const std::string& InModel, const std::string& InKey ) const
{
    const auto model = m_models.find( InModel );
    if( model == m_models.end() )
    {
        return {};
    }
    const auto value = model->second.Values.find( InKey );
    if( value == model->second.Values.end() )
    {
        return {};
    }
    return value->second.IsString ? value->second.String : std::to_string( value->second.Number );
}


std::string RmlUiCore::ModelPollEvent( const std::string& InModel )
{
    auto model = m_models.find( InModel );
    if( model == m_models.end() || model->second.EventQueue.empty() )
    {
        return {};
    }
    std::string event = std::move( model->second.EventQueue.front() );
    model->second.EventQueue.pop_front();
    return event;
}


void RmlUiCore::SyncModels()
{
    bool materialized = false;
    for( auto& [name, model] : m_models )
    {
        Rml::DataModelConstructor constructor;
        if( !model.Created )
        {
            constructor = m_context->CreateDataModel( name );
            if( !constructor )
            {
                constructor = m_context->GetDataModel( name );
            }
            if( !constructor )
            {
                continue;
            }
            model.Created = true;
            materialized = true;
        }
        Rml::DataModelHandle handle;
        for( auto& [key, value] : model.Values )
        {
            if( !value.Bound )
            {
                if( !constructor )
                {
                    constructor = m_context->GetDataModel( name );
                }
                ModelValue* stored = &value;
                constructor.BindFunc(
                    key,
                    [stored]( Rml::Variant& OutValue ) {
                        if( stored->IsString )
                        {
                            OutValue = stored->String;
                        }
                        else
                        {
                            OutValue = stored->Number;
                        }
                    },
                    [stored]( const Rml::Variant& InValue ) {
                        // Two-way bindings (data-value on inputs) write the view's value back.
                        if( stored->IsString )
                        {
                            stored->String = InValue.Get<Rml::String>();
                        }
                        else
                        {
                            stored->Number = InValue.Get<double>();
                        }
                    } );
                value.Bound = true;
                value.Dirty = false;
                materialized = true;
                continue;
            }
            if( value.Dirty )
            {
                if( !handle )
                {
                    handle = m_context->GetDataModel( name ).GetModelHandle();
                }
                handle.DirtyVariable( key );
                value.Dirty = false;
            }
        }
        for( auto& [key, bound] : model.Events )
        {
            if( bound )
            {
                continue;
            }
            if( !constructor )
            {
                constructor = m_context->GetDataModel( name );
            }
            Model* owner = &model;
            const std::string eventName = key;
            constructor.BindEventCallback( key, [owner, eventName]( Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& ) {
                owner->EventQueue.push_back( eventName );
            } );
            bound = true;
            materialized = true;
        }
    }

    if( materialized )
    {
        // Documents resolve their data bindings at load: anything already on screen that
        // predates the model (play started after the scene loaded) reloads once to see it.
        m_modelsMaterialized = true;
    }
    if( m_modelsMaterialized )
    {
        m_modelsMaterialized = false;
        int reloaded = 0;
        for( Entity& entity : GetEntities() )
        {
            UIDocument& document = entity.GetComponent<UIDocument>();
            if( document.m_document != nullptr )
            {
                CloseDocument( document );
                LoadDocument( document );
                ++reloaded;
            }
        }
        for( const Overlay& overlay : m_overlays )
        {
            reloaded += overlay.Document != nullptr ? 1 : 0;
        }
        CloseOverlays();
        if( reloaded > 0 )
        {
            INFO( "UI", "UI: rebound " + std::to_string( reloaded ) + " document(s) to script models" );
        }
    }
}


// --- Text scale, language, key injection (UI-4) --------------------------------------------------

void RmlUiCore::SetTextScale( float InScale )
{
    m_textScale = std::max( 0.25f, InScale );
    if( m_context != nullptr )
    {
        m_context->SetDensityIndependentPixelRatio( m_textScale );
    }
    INFO( "UI", "UI: text scale " + std::to_string( m_textScale ) );
}


void RmlUiCore::SetLanguage( const std::string& InCode )
{
    m_language = InCode;
    Localization::Get().SetLanguage( InCode );
    // Translation happens at document load: rebuild what's on screen.
    m_modelsMaterialized = true;
    INFO( "UI", "UI: language '" + InCode + "' (" + std::to_string( Localization::LoadTable( InCode ).size() ) + " strings)" );
}


void RmlUiCore::InjectUiKey( int InKeyIdentifier, int InModifiers )
{
    if( m_context != nullptr )
    {
        m_context->ProcessKeyDown( static_cast<Rml::Input::KeyIdentifier>( InKeyIdentifier ), InModifiers );
        m_context->ProcessKeyUp( static_cast<Rml::Input::KeyIdentifier>( InKeyIdentifier ), InModifiers );
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
    SyncModels();

    LoadOverlays();
    bool anyVisible = std::any_of( m_overlays.begin(), m_overlays.end(), []( const Overlay& InOverlay ) { return InOverlay.Document != nullptr; } );
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

    // Settings-bound elements (RmlSettings) follow the player settings and rebinds.
    if( RmlSettings::ConsumeChanged() )
    {
        for( Entity& entity : GetEntities() )
        {
            UIDocument& document = entity.GetComponent<UIDocument>();
            if( document.m_document != nullptr )
            {
                RmlSettings::Refresh( *static_cast<Rml::ElementDocument*>( document.m_document ) );
            }
        }
        for( Overlay& overlay : m_overlays )
        {
            if( overlay.Document != nullptr )
            {
                RmlSettings::Refresh( *overlay.Document );
            }
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
