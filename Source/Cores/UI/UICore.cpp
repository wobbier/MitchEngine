#include <PCH.h>
#include <Cores/UI/UICore.h>

#include <Components/Camera.h>
#include <Engine/Engine.h>

#if USING( ME_UI )
#include <AppCore/Overlay.h>
#include <AppCore/Platform.h>
#include <Ultralight/platform/Platform.h>

static SDL_Cursor* g_ul_to_sdl_cursor[ultralight::kCursor_Custom];

#endif

#include <imgui.h>
#include <Utils/ImGuiUtils.h>
#include <Renderer.h>
#include "Mathf.h"
#include "Dementia.h"
#include "Events/SceneEvents.h"
#include "CLog.h"
#include "Utils/BGFXUtils.h"
#include "Primitives/Cube.h"
#include "UI/FileLogger.h"
#include "UI/FileSystemBasic.h"
#include "UI/UIUtils.h"
#include "UI/Platform/UIClipboard.h"

UICore::UICore( IWindow* window, BGFXRenderer* renderer )
    : Base( ComponentFilter().Requires<BasicUIView>() )
    , m_uiTexture( BGFX_INVALID_HANDLE )
{
    SetIsSerializable( false );
    ///DestroyOnLoad = false;

    std::vector<TypeId> events;
    events.push_back( SceneLoadedEvent::GetEventId() );
    EventManager::GetInstance().RegisterReceiver( this, events );

    m_renderer = renderer;

    UIProgram = Moonlight::LoadProgram( "Assets/Shaders/UI.vert", "Assets/Shaders/UI.frag" );
    s_texUI = bgfx::createUniform( "s_texUI", bgfx::UniformType::Sampler );

#if USING( ME_UI )
    m_config.force_repaint = false;
    m_config.face_winding = ultralight::FaceWinding::Clockwise;
    m_config.animation_timer_delay = 1.0 / 60.0;
    m_config.recycle_delay = 1.0;
#if USING( ME_EDITOR )
    m_config.resource_path_prefix = "Assets/UI/";
#else
    m_config.resource_path_prefix = "Assets/UI/";
#endif


    ultralight::Platform::instance().set_config( m_config );
    ultralight::Platform::instance().set_clipboard( new UIClipboard() );
    // maybe make this customizable, I would like to load assets out the UI folder maybe?
    ultralight::Platform::instance().set_file_system( new FileSystemBasic( Path( "Assets/UI" ).FullPath.c_str() ) );
    ultralight::Platform::instance().set_logger( new FileLogger( "ultralight.log" ) );

#if USING( ME_PLATFORM_WINDOWS )
    ultralight::Platform::instance().set_font_loader( new FontLoaderWin() );
#endif
#if USING( ME_PLATFORM_MACOS )
    ultralight::Platform::instance().set_font_loader( new FontLoaderMac() );
#endif

    m_driver = new UIDriver();
    ultralight::Platform::instance().set_gpu_driver( m_driver );

    m_uiRenderer = ultralight::Renderer::Create();

    memset( &g_ul_to_sdl_cursor[0], 0, sizeof( SDL_Cursor* ) * ultralight::kCursor_Custom );
    g_ul_to_sdl_cursor[ultralight::kCursor_Pointer] = SDL_CreateSystemCursor( SDL_SYSTEM_CURSOR_ARROW );
    g_ul_to_sdl_cursor[ultralight::kCursor_IBeam] = SDL_CreateSystemCursor( SDL_SYSTEM_CURSOR_IBEAM );
    g_ul_to_sdl_cursor[ultralight::kCursor_Hand] = SDL_CreateSystemCursor( SDL_SYSTEM_CURSOR_HAND );
    g_ul_to_sdl_cursor[ultralight::kCursor_NorthSouthResize] = SDL_CreateSystemCursor( SDL_SYSTEM_CURSOR_SIZENS );
    g_ul_to_sdl_cursor[ultralight::kCursor_EastWestResize] = SDL_CreateSystemCursor( SDL_SYSTEM_CURSOR_SIZEWE );
    g_ul_to_sdl_cursor[ultralight::kCursor_NorthEastSouthWestResize] = SDL_CreateSystemCursor( SDL_SYSTEM_CURSOR_SIZENESW );
    g_ul_to_sdl_cursor[ultralight::kCursor_NorthWestSouthEastResize] = SDL_CreateSystemCursor( SDL_SYSTEM_CURSOR_SIZENWSE );

#endif
}

UICore::~UICore()
{
    CLog::Log( CLog::LogType::Debug, "UICore Destroyed..." );
    EventManager::GetInstance().DeRegisterReciever( this );

#if USING( ME_UI )
    for( int ui_cursor = 0; ui_cursor < (int)ultralight::kCursor_Custom; ui_cursor++ )
    {
        SDL_FreeCursor( g_ul_to_sdl_cursor[ui_cursor] );
    }
#endif

    // Am I leaking? or am I just dreaming?
    //m_overlays.clear();

#if USING( ME_UI )
    delete m_driver;
#endif
}

void UICore::Init()
{
    CLog::Log( CLog::LogType::Debug, "UICore Initialized..." );
}

void UICore::OnEntityAdded( Entity& NewEntity )
{
    BasicUIView& view = NewEntity.GetComponent<BasicUIView>();

    InitUIView( view );
}

void UICore::OnEntityRemoved( Entity& InEntity )
{
    BasicUIView& view = InEntity.GetComponent<BasicUIView>();
    view.IsInitialized = false;

#if USING( ME_UI )
    m_views.erase( std::remove( m_views.begin(), m_views.end(), m_views[view.Index] ), m_views.end() );
#endif
}

void UICore::OnStop()
{
#if USING( ME_UI )
    m_views.clear();
#endif
}

void UICore::Update( const UpdateContext& inUpdateContext )
{
    OPTICK_EVENT( "UI Update", Optick::Category::UI );
    auto& entities = GetEntities();
    for( auto& InEntity : entities )
    {
        if( !InEntity.HasComponent<BasicUIView>() )
        {
            BRUH( "Updating an entity that doesn't have a UI View." );
            continue;
        }

        BasicUIView& view = InEntity.GetComponent<BasicUIView>();
        if( !view.IsInitialized )
        {
            InitUIView( view );
        }

        view.OnUpdate( inUpdateContext.GetDeltaTime() );
    }
#if USING( ME_UI )
    Input& gameInput = GetEngine().GetInput();
    ultralight::MouseEvent mouseEvent;
    ultralight::ScrollEvent mouseScrollEvent;
    mouseEvent.type = ultralight::MouseEvent::kType_MouseMoved;
    Vector2 mousePosition = gameInput.GetMousePosition();
    mouseScrollEvent.delta_y = gameInput.GetMouseScrollDelta().y * 100;
    mouseScrollEvent.delta_x = 0;

    bool isShiftDown = gameInput.IsKeyDown( KeyCode::LeftShift ) || gameInput.IsKeyDown( KeyCode::RightShift );
    bool isCtrlDown = gameInput.IsKeyDown( KeyCode::LeftControl ) || gameInput.IsKeyDown( KeyCode::RightControl );
    bool isAltDown = gameInput.IsKeyDown( KeyCode::LeftAlt ) || gameInput.IsKeyDown( KeyCode::RightAlt );
    uint8_t keyMods = 0;

    if( isShiftDown ) keyMods |= ultralight::KeyEvent::Modifiers::kMod_ShiftKey;
    if( isCtrlDown )  keyMods |= ultralight::KeyEvent::Modifiers::kMod_CtrlKey;
    if( isAltDown )   keyMods |= ultralight::KeyEvent::Modifiers::kMod_AltKey;

    {
        OPTICK_EVENT( "UI Keyboard Update", Optick::Category::UI );
        for( auto& inputEvent : gameInput.m_keyEventsThisFrame )
        {
            ultralight::KeyEvent keyEvent;
            bool isCharacterEvent = UIUtils::ConvertToUL( (KeyCode)inputEvent.Key, keyEvent.virtual_key_code ) && !isCtrlDown && !isAltDown;
            keyEvent.native_key_code = inputEvent.Key;

            // #TODO: Modifiers / Keypad detection
            keyEvent.is_system_key = false;
            keyEvent.is_keypad = false;
            keyEvent.modifiers = keyMods;
            keyEvent.is_auto_repeat = inputEvent.State == KeyState::Held;

            ultralight::KeyEvent::Type keyEventType = KeyEvent::kType_RawKeyDown;

            if( isCharacterEvent && inputEvent.State != KeyState::Released )
            {
                keyEventType = KeyEvent::kType_Char;
                GetKeyFromVirtualKeyCode( keyEvent.virtual_key_code, isShiftDown, keyEvent.text );
            }
            else
            {
                if( inputEvent.State == KeyState::Released )
                    keyEventType = KeyEvent::kType_KeyUp;
            }

            keyEvent.type = keyEventType;
            for( auto& view : m_views )
            {
                view->FireKeyEvent( keyEvent );
            }
        }
    }

#if USING( ME_EDITOR )
    //if ( !static_cast<EditorApp*>( GetEngine().GetGame() )->IsGameRunning() )
    //{
    //    return;
    //}

    //Havana* editor = static_cast<EditorCore*>( GetEngine().GetWorld().lock()->GetCore( EditorCore::GetTypeId() ) )->GetEditor();

    Vector2 windowPosition = GetEngine().GetWindow()->GetPosition();
    Vector2 offset = gameInput.GetMouseOffset();
    mouseEvent.x = ( windowPosition.x + mousePosition.x ) - offset.x;// + windowPosition.x  + offset.x;
    mouseEvent.y = ( windowPosition.y + mousePosition.y ) - offset.y;// + windowPosition.y  - offset.y;

    if( mousePosition.IsZero() )
    {
        return;
    }
#else
    mouseEvent.x = mousePosition.x;
    mouseEvent.y = mousePosition.y;
#endif

    // This sucks
    static bool hasPressed = false;
    if( gameInput.WasMouseButtonPressed( MouseButton::Left ) && !hasPressed )
    {
        mouseEvent.button = ultralight::MouseEvent::Button::kButton_Left;
        mouseEvent.type = ultralight::MouseEvent::kType_MouseDown;
        hasPressed = true;
    }
    else if( gameInput.IsMouseButtonDown( MouseButton::Left ) )
    {
        mouseEvent.button = ultralight::MouseEvent::Button::kButton_Left;
        mouseEvent.type = ultralight::MouseEvent::kType_MouseMoved;
    }
    else if( !gameInput.WasMouseButtonPressed( MouseButton::Left ) && hasPressed )
    {
        mouseEvent.button = ultralight::MouseEvent::Button::kButton_Left;
        mouseEvent.type = ultralight::MouseEvent::kType_MouseUp;
        hasPressed = false;
    }
    else
    {
        mouseEvent.button = ultralight::MouseEvent::Button::kButton_None;
    }

#if USING( ME_EDITOR )
    //if (m_renderer->GetViewportMode() == ViewportMode::Game)
#endif
    {
        OPTICK_EVENT( "UI Input Update", Optick::Category::UI );
        for( auto& view : m_views )
        {
            view->FireMouseEvent( mouseEvent );
            view->FireScrollEvent( mouseScrollEvent );
        }
    }

    // Update internal logic (timers, event callbacks, etc.)
    {
        OPTICK_EVENT( "UI Widget Update", Optick::Category::UI );
        m_uiRenderer->Update();
    }
#endif
}

void UICore::Render()
{
    OPTICK_EVENT( "UI Render", Optick::Category::GPU_UI );

#if USING( ME_UI )

    {
        OPTICK_EVENT( "Ultralight Render", Optick::Category::UI );
        m_uiRenderer->Render();
    }
    m_driver->RenderCommandList();

    for( auto ent : GetEntities() )
    {
        if( !ent.HasComponent<BasicUIView>() )
        {
            BRUH( "Rendering an entity that doesn't have a UI View." );
            continue;
        }
        OPTICK_EVENT( "UI View Render", Optick::Category::GPU_UI );

        ultralight::RenderTarget surface = (ultralight::RenderTarget)( ent.GetComponent<BasicUIView>().ViewRef->render_target() );

        bgfx::ViewId view = 10;
        {
            bgfx::setViewName( view, "UI BLIT" );
            const bgfx::RendererType::Enum renderer = bgfx::getRendererType();
            float m_texelHalf = 0.0f;
            float orthoProj[16];
            bx::mtxOrtho( orthoProj, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, bgfx::getCaps()->homogeneousDepth );
            {
                // clear out transform stack
                float identity[16];
                bx::mtxIdentity( identity );
                bgfx::setTransform( identity );
            }

            bgfx::setViewRect( view, 0, 0, uint16_t( surface.texture_width ), uint16_t( surface.texture_height ) );
            bgfx::setViewTransform( view, NULL, orthoProj );
            bgfx::setViewFrameBuffer( view, m_driver->m_buffers[surface.render_buffer_id].BufferHandle );
            bgfx::setState( 0
                | BGFX_STATE_WRITE_RGB
                //| BGFX_STATE_BLEND_ALPHA // - Not it, creates artifacts
                //| BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_INV_SRC_ALPHA) // - Almost there
                | BGFX_STATE_BLEND_FUNC_SEPARATE( BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_INV_SRC_ALPHA, BGFX_STATE_BLEND_INV_DST_ALPHA, BGFX_STATE_BLEND_ONE )
            );
            bgfx::setTexture( 0, s_texUI, m_driver->m_buffers[surface.render_buffer_id].TexHandle );
            Moonlight::screenSpaceQuad( surface.texture_width, surface.texture_height, m_texelHalf, bgfx::getCaps()->originBottomLeft );
            bgfx::submit( view, UIProgram );
            bgfx::blit( view, m_uiTexture, 0, 0, m_driver->m_buffers[surface.render_buffer_id].TexHandle );
        }
        if( !surface.is_empty )//&& !surface->dirty_bounds().IsEmpty() )
        {
            //m_driver->m_storedTextures[surface.texture_id].Handle
            //bgfx::blit( m_driver->kViewId + surface.render_buffer_id, m_uiTexture, 0, 0, m_driver->m_buffers[surface.render_buffer_id].TexHandle);

            //CopyBitmapToTexture( surface->bitmap() );
            GetEngine().GetRenderer().GetCameraCache().Get( Camera::CurrentCamera->GetCameraId() )->UITexture = m_uiTexture;// m_driver->m_storedTextures[surface.texture_id].Handle;

            //surface->ClearDirtyBounds();
        }
    }
#endif
}


void UICore::PostRender( const UpdateContext& inUpdateContext )
{
#if USING( ME_UI )
    m_uiRenderer->RefreshDisplay( 0 );
#endif
}


void UICore::OnResize( const Vector2& NewSize )
{
    if( NewSize.IsZero() )
    {
        return;
    }

    if( NewSize != UISize )
    {
        if( bgfx::isValid( m_uiTexture ) )
        {
            bgfx::destroy( m_uiTexture );
        }

        m_uiTexture = bgfx::createTexture2D( static_cast<uint16_t>( Mathf::Max( NewSize.x, 1.f ) )
            , static_cast<uint16_t>( Mathf::Abs( Mathf::Max( NewSize.y, 1.f ) ) )
            , false
            , 1
            , bgfx::TextureFormat::BGRA8
            , BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT | BGFX_SAMPLER_MIP_POINT | BGFX_TEXTURE_BLIT_DST
        );
        UISize = NewSize;

#if USING( ME_UI )
    //if( m_context )
        {
            //m_context->Resize( NewSize );
            for( auto overlay : m_views )
            {
                overlay->Resize( (int)NewSize.x, (int)NewSize.y );
            }
        }
#endif

#if USING ( ME_DEBUG )
        BRUH("{}, {}", m_uiTexture.idx, "UI Tex");
#endif
    }
}

void UICore::InitUIView( BasicUIView& view )
{
#if USING( ME_UI )
    ultralight::ViewConfig view_config;
    view_config.is_accelerated = true;
    view_config.is_transparent = true;
    view_config.font_family_standard = "Arial";

    ultralight::RefPtr<ultralight::View> newView;
    newView = m_uiRenderer->CreateView( static_cast<uint32_t>( Camera::CurrentCamera->OutputSize.x ), static_cast<uint32_t>( Camera::CurrentCamera->OutputSize.y ), view_config, nullptr );

    //ultralight::Ref<ultralight::View> newView = m_uiRenderer->CreateView( static_cast<uint32_t>( Camera::CurrentCamera->OutputSize.x ), static_cast<uint32_t>( Camera::CurrentCamera->OutputSize.y ), true, nullptr );

    //ultralight::RefPtr<ultralight::Overlay> overlay = ultralight::Overlay::Create( *m_window.get(), newView, 0, 0 );
    newView->set_load_listener( &view );
    newView->set_view_listener( this );

    if( m_useWebUrl && !view.m_uiUrl.empty() )
    {
        newView->LoadURL( ultralight::String( view.m_uiUrl.c_str() ) );
    }
    else
    {
        // Load from disk
        if (view.FilePath.Exists)
        {
            // Read from file path
            ultralight::String str = "file:///" + ultralight::String(view.FilePath.GetLocalPath().data());
            newView->LoadURL(str);
        }
        else
        {
            // the file doesn't exist so display the raw html (error screen)
            ultralight::String str = ultralight::String(view.SourceFile.Read().c_str());
            newView->LoadHTML(str);
        }
    }

    //m_overlays.push_back( overlay );
    //GetOverlayManager()->Add( overlay.get() );
    m_views.push_back( newView );

    view.IsInitialized = true;
    view.Index = m_views.size() - 1;
    view.ViewRef = newView;
#endif
}
//
//ultralight::OverlayManager* UICore::GetOverlayManager()
//{
//    return this;
//}

#if USING( ME_UI )

void UICore::CopyBitmapToTexture( ultralight::RefPtr<ultralight::Bitmap> bitmap )
{
    void* pixels = bitmap->LockPixels();

    uint32_t width = bitmap->width();
    uint32_t height = bitmap->height();
    uint32_t stride = bitmap->row_bytes();

    //bitmap->WritePNG(Path("Assets/TestUI.png").FullPath.c_str());

    {
        const uint16_t tw = static_cast<uint32_t>( bitmap->width() );
        const uint16_t th = static_cast<uint32_t>( bitmap->height() );
        const uint16_t tx = 0;
        const uint16_t ty = 0;

        const bgfx::Memory* mem = bgfx::makeRef( pixels, stride );

        if( bgfx::isValid( m_uiTexture ) && Camera::CurrentCamera )
        {
            bgfx::updateTexture2D( m_uiTexture, 0, 0, tx, ty, tw, th, mem, stride );
            GetEngine().GetRenderer().GetCameraCache().Get( Camera::CurrentCamera->GetCameraId() )->UITexture = m_uiTexture;
        }
    }

    bitmap->UnlockPixels();
}

void UICore::OnChangeCursor( ultralight::View* caller, ultralight::Cursor cursor )
{
    if( g_ul_to_sdl_cursor[cursor] )
    {
        SDL_SetCursor( g_ul_to_sdl_cursor[cursor] );
    }
}

void UICore::OnAddConsoleMessage( ultralight::View* caller, const ultralight::ConsoleMessage& message )
{
    BRUH( message.message().utf8().data() );
}


#endif

bool UICore::OnEvent( const BaseEvent& evt )
{
    if( evt.GetEventId() == SceneLoadedEvent::GetEventId() )
    {
        const SceneLoadedEvent& event = static_cast<const SceneLoadedEvent&>( evt );
        //m_overlays.clear();
        // O_O
    }
    return false;
}

#if USING( ME_EDITOR )

void UICore::OnEditorInspect()
{
    Base::OnEditorInspect();
#if USING ( ME_UI )
    ImGui::Text( "Clear Calls %i", m_driver->m_uiDrawInfo.m_numClearCalls );
    ImGui::Text( "Fill Calls %i", m_driver->m_uiDrawInfo.m_numDrawFillCalls );
    ImGui::Text( "Fill Path Calls %i", m_driver->m_uiDrawInfo.m_numDrawFillPathCalls );
    if( ImGui::CollapsingHeader( "Settings", ImGuiTreeNodeFlags_DefaultOpen ) )
    {
        ImGui::Checkbox( "Use Web URL", &m_useWebUrl );
        if( ImGui::Checkbox( "Force Repaint", &m_config.force_repaint ) )
        {
            ultralight::Platform::instance().set_config( m_config );
        }
    }

    if( ImGui::CollapsingHeader( "Final UI Texture", ImGuiTreeNodeFlags_DefaultOpen ) )
    {
        ImGui::Image( m_uiTexture, ImVec2( UISize.x, UISize.y ) );
    }

    if( ImGui::CollapsingHeader( "UI Buffers", ImGuiTreeNodeFlags_DefaultOpen ) )
    {
        for( auto& buffer : m_driver->m_buffers )
        {
            ImGui::Text( std::string( "Frame Buffer: " + std::to_string( buffer.first ) + ", " + std::to_string( buffer.second.FrameBufferTexture ) ).c_str() );
            ImGui::Image( buffer.second.TexHandle, ImVec2( m_driver->m_storedTextures[buffer.second.FrameBufferTexture].Width, m_driver->m_storedTextures[buffer.second.FrameBufferTexture].Height ) );
        }
    }

    if( ImGui::CollapsingHeader( "UI Textures", ImGuiTreeNodeFlags_DefaultOpen ) )
    {
        for( auto& buffer : m_driver->m_storedTextures )
        {
            if( !buffer.second.IsRenderTexture )
            {
                ImGui::Text( std::string( "Texture: " + std::to_string( buffer.first ) ).c_str() );
                ImGui::Image( buffer.second.Handle, ImVec2( buffer.second.Width, buffer.second.Height ) );
            }
        }
    }
#endif
}

#endif
