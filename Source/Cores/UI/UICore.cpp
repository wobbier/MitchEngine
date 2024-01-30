#include <PCH.h>
#include <Cores/UI/UICore.h>

#include <AppCore/Overlay.h>
#include <Components/Camera.h>
#include <Engine/Engine.h>
#include <Ultralight/platform/Platform.h>
#include <Ultralight/platform/Config.h>
#include <imgui.h>
#include <Utils/ImGuiUtils.h>
#include <AppCore/Platform.h>
#include <Renderer.h>
#include "Mathf.h"
#include "Dementia.h"
#include "Events/SceneEvents.h"
#include "CLog.h"
#include "Utils/BGFXUtils.h"
#include "Primitives/Cube.h"

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

    ultralight::Config config;
    config.force_repaint = true;
    config.face_winding = ultralight::FaceWinding::Clockwise;

    ultralight::Platform::instance().set_config( config );
    ultralight::Platform::instance().set_font_loader( new FontLoaderWin() );
    ultralight::Platform::instance().set_file_system( ultralight::GetPlatformFileSystem( "." ) );
    ultralight::Platform::instance().set_logger( ultralight::GetDefaultLogger( "ultralight.log" ) );

    m_driver = new UIDriver();
    ultralight::Platform::instance().set_gpu_driver( m_driver );

    m_uiRenderer = ultralight::Renderer::Create();
}

UICore::~UICore()
{
    CLog::Log( CLog::LogType::Debug, "UICore Destroyed..." );
    EventManager::GetInstance().DeRegisterReciever( this );
    // Am I leaking? or am I just dreaming?
    //m_overlays.clear();
    delete m_driver;
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

    //auto overlay = m_overlays[view.Index];
    //GetOverlayManager()->Remove( overlay.get() );
    //m_overlays.erase( std::remove( m_overlays.begin(), m_overlays.end(), overlay ), m_overlays.end() );
    ////m_overlays.erase( m_overlays.begin() + view.Index );
}

void UICore::OnStop()
{
    //for( auto overlay : m_overlays )
    //{
    //    GetOverlayManager()->Remove( overlay.get() );
    //}
    //m_overlays.clear();
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
    }
    Input& gameInput = GetEngine().GetInput();
    ultralight::MouseEvent mouseEvent;
    ultralight::ScrollEvent mouseScrollEvent;
    mouseEvent.type = ultralight::MouseEvent::kType_MouseMoved;
    Vector2 mousePosition = gameInput.GetMousePosition();
    mouseScrollEvent.delta_y = gameInput.GetMouseScrollDelta().y * 100;

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

    static bool hasPressed = false;
    if( gameInput.WasMouseButtonPressed( MouseButton::Left ) && !hasPressed )
    {
        mouseEvent.button = ultralight::MouseEvent::Button::kButton_Left;
        mouseEvent.type = ultralight::MouseEvent::kType_MouseDown;
        hasPressed = true;
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
}

void UICore::Render()
{
    OPTICK_EVENT( "UI Render", Optick::Category::GPU_UI );

    m_uiRenderer->Render();
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

        bgfx::ViewId view = 68;
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

        m_uiTexture = bgfx::createTexture2D( static_cast<uint16_t>( NewSize.x )
            , static_cast<uint16_t>( Mathf::Abs( NewSize.y ) )
            , false
            , 1
            , bgfx::TextureFormat::BGRA8
            , BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT | BGFX_SAMPLER_MIP_POINT | BGFX_TEXTURE_BLIT_DST
        );
        UISize = NewSize;

    //if( m_context )
        {
            //m_context->Resize( NewSize );
            for( auto overlay : m_views )
            {
                overlay->Resize( (int)NewSize.x, (int)NewSize.y );
            }
        }

#if USING ( ME_DEBUG )
        BRUH_FMT( "%i, %s", m_uiTexture.idx, "UI Tex" );
#endif
    }
}

void UICore::InitUIView( BasicUIView& view )
{
    ultralight::ViewConfig view_config;
    view_config.is_accelerated = true;
    view_config.is_transparent = true;
    view_config.font_family_standard = "Arial";

    ultralight::RefPtr<ultralight::View> newView;
    newView = m_uiRenderer->CreateView( static_cast<uint32_t>( Camera::CurrentCamera->OutputSize.x ), static_cast<uint32_t>( Camera::CurrentCamera->OutputSize.y ), view_config, nullptr );

    //ultralight::Ref<ultralight::View> newView = m_uiRenderer->CreateView( static_cast<uint32_t>( Camera::CurrentCamera->OutputSize.x ), static_cast<uint32_t>( Camera::CurrentCamera->OutputSize.y ), true, nullptr );

    //ultralight::RefPtr<ultralight::Overlay> overlay = ultralight::Overlay::Create( *m_window.get(), newView, 0, 0 );
    newView->set_load_listener( &view );

    ////overlay->view()->LoadHTML(view.SourceFile.Read().c_str());
    ultralight::String str = "file:///" + ultralight::String( view.FilePath.GetLocalPath().data() );
    newView->LoadURL( str );

    //m_overlays.push_back( overlay );
    //GetOverlayManager()->Add( overlay.get() );
    m_views.push_back( newView );

    view.IsInitialized = true;
    view.Index = m_views.size() - 1;
    view.ViewRef = newView;
}
//
//ultralight::OverlayManager* UICore::GetOverlayManager()
//{
//    return this;
//}

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
    ImGui::Text( "Clear Calls %i", m_driver->m_uiDrawInfo.m_numClearCalls );
    ImGui::Text( "Fill Calls %i", m_driver->m_uiDrawInfo.m_numDrawFillCalls );
    ImGui::Text( "Fill Path Calls %i", m_driver->m_uiDrawInfo.m_numDrawFillPathCalls );
    if( ImGui::CollapsingHeader( "Main UI Texture", ImGuiTreeNodeFlags_DefaultOpen ) )
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
}

#endif
