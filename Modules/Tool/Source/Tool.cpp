#include "Tool.h"
#include "Dementia.h"

#include <functional>

#include "SDL_video.h"

#include "Renderer.h"
#include "Math/Vector2.h"
#include "Window/SDLWindow.h"
#include "imgui.h"
#include "ImGuiTheme.h"
#include "Window/PlatformWindowHooks.h"
#include <Camera/CameraData.h>

extern bool ImGui_ImplSDL2_InitForOther( SDL_Window* window );
extern void ImGui_ImplSDL2_Shutdown();
static SDL_Cursor* g_imgui_to_sdl_cursor[ImGuiMouseCursor_COUNT];

Tool::Tool( ToolCreationFlags& inToolCreationFlags )
    : m_toolCreationFlags( inToolCreationFlags )
{
}

Tool::~Tool()
{
    Shutdown();
}

void Tool::Start()
{
    if( m_renderer )
    {
        return;
    }

    m_renderer = new BGFXRenderer();

    std::function<void( const Vector2& )> ResizeFunc = [this]( const Vector2& NewSize )
        {
            if( m_renderer )
            {
                m_renderer->WindowResized( NewSize );
            }
        };

    // X 0 centers the window.
    m_window = new SDLWindow( m_toolCreationFlags.toolName, ResizeFunc, 0, 0, m_toolCreationFlags.windowSize );
    m_window->SetBorderless( m_toolCreationFlags.isBorderless );
    ResizeFunc( m_toolCreationFlags.windowSize );
    RendererCreationSettings set;
    set.WindowPtr = m_window->GetWindowPtr();
#if USING( ME_PLATFORM_LINUX )
    set.DisplayPtr = static_cast<SDLWindow*>( m_window )->GetDisplayPtr();
    set.WindowType = static_cast<SDLWindow*>( m_window )->GetWindowType();
#endif
    set.InitialSize = m_window->GetSize();
    set.InitAssets = false;
    m_renderer->Create( set );

    // As the engine does: bgfx creates the graphics surfaces itself.
    ImGui_ImplSDL2_InitForOther( static_cast<SDLWindow*>( m_window )->WindowHandle );

    g_imgui_to_sdl_cursor[ImGuiMouseCursor_Arrow] = SDL_CreateSystemCursor( SDL_SYSTEM_CURSOR_ARROW );
    g_imgui_to_sdl_cursor[ImGuiMouseCursor_TextInput] = SDL_CreateSystemCursor( SDL_SYSTEM_CURSOR_IBEAM );
    g_imgui_to_sdl_cursor[ImGuiMouseCursor_Hand] = SDL_CreateSystemCursor( SDL_SYSTEM_CURSOR_HAND );
    g_imgui_to_sdl_cursor[ImGuiMouseCursor_ResizeNS] = SDL_CreateSystemCursor( SDL_SYSTEM_CURSOR_SIZENS );
    g_imgui_to_sdl_cursor[ImGuiMouseCursor_ResizeEW] = SDL_CreateSystemCursor( SDL_SYSTEM_CURSOR_SIZEWE );
    g_imgui_to_sdl_cursor[ImGuiMouseCursor_ResizeNESW] = SDL_CreateSystemCursor( SDL_SYSTEM_CURSOR_SIZENESW );
    g_imgui_to_sdl_cursor[ImGuiMouseCursor_ResizeNWSE] = SDL_CreateSystemCursor( SDL_SYSTEM_CURSOR_SIZENWSE );

    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = m_toolCreationFlags.layoutFile.empty() ? nullptr : m_toolCreationFlags.layoutFile.c_str();

    if( m_toolCreationFlags.isDockingEnabled )
    {
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    }

    // The editor's look (OnStart can apply the user's own).
    ImGuiTheme::Apply( ImGuiTheme::Mode::Dark, ImGuiTheme::DefaultAccent() );

    // Hooking into ImGui
    {
        ImGui::InitHooks();
    }

    // Setup the borderless window
    {
        auto cb = [this]( const Vector2& pos ) -> std::optional<SDL_HitTestResult>
            {
                if( pos > TitleBarDragPosition && pos < TitleBarDragPosition + TitleBarDragSize )
                {
                    return SDL_HitTestResult::SDL_HITTEST_DRAGGABLE;
                }

                return std::nullopt;
            };
        m_window->SetBorderless( m_toolCreationFlags.isBorderless );
        m_window->SetCustomDragCallback( cb );
    }

    OnStart();

    Run();

    OnEnd();
    Shutdown();
}

void Tool::RequestScreenshot( const std::string& InPath )
{
    if( m_renderer )
    {
        m_renderer->RequestScreenshot( InPath );
    }
}

uint32_t Tool::GetScreenshotCount() const
{
    return m_renderer ? m_renderer->GetScreenshotCount() : 0;
}

void Tool::Run()
{
    while( !m_window->ShouldClose() && !m_closeRequested )
    {
        m_window->ParseMessageQueue();

        m_input.Update();
        ImGuiIO& io = ImGui::GetIO();

        Vector2 mousePos = m_input.GetMousePosition();
        if( io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable )
        {
            // Multi-viewport mode: mouse position in OS absolute coordinates (io.MousePos is (0,0) when the mouse is on the upper-left of the primary monitor)
            mousePos = m_input.GetGlobalMousePosition();
        }

        m_renderer->BeginFrame( mousePos, ( m_input.IsMouseButtonDown( MouseButton::Left ) ? 0x01 : 0 )
            | ( m_input.IsMouseButtonDown( MouseButton::Right ) ? 0x02 : 0 )
            | ( m_input.IsMouseButtonDown( MouseButton::Middle ) ? 0x04 : 0 )
            , (int32_t)m_input.GetMouseScrollOffset().y
            , m_window->GetSize()
            , -1
            , Moonlight::RenderView::ImGuiMain );

        if( m_toolCreationFlags.isDockingEnabled )
        {
            ImGuiViewport* viewport = ImGui::GetMainViewport();

            ImVec2 DockSize = viewport->Size;
            DockSize.x -= DockMargins.z + DockMargins.w;
            DockSize.y -= DockMargins.x + DockMargins.y;

            ImVec2 DockPos;
            DockPos.x = DockMargins.z;
            DockPos.y = DockMargins.x;

            ImGui::SetNextWindowPos( DockPos );
            ImGui::SetNextWindowSize( DockSize );
            ImGui::SetNextWindowViewport( viewport->ID );
            ImGui::SetNextWindowBgAlpha( 0.0f );

            ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoDocking;
            window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
            window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

            ImGui::PushStyleVar( ImGuiStyleVar_WindowRounding, 0.0f );
            ImGui::PushStyleVar( ImGuiStyleVar_WindowBorderSize, 0.0f );
            ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( 0.0f, 0.0f ) );
            bool show_dockspace = true;
            ImGui::Begin( "MainDockSpace", &show_dockspace, window_flags );
            ImGui::PopStyleVar( 3 );
            ImGuiID dockspace_id = ImGui::GetID( "MyDockspace" );
            ImGuiDockNodeFlags dockspace_flags = ImGuiDockNodeFlags_PassthruCentralNode;
            {
                ImGui::DockSpace( dockspace_id, ImVec2( 0.0f, 0.0f ), dockspace_flags );
                OnUpdate();
                ImGui::End();
            }
        }
        else
        {
            OnUpdate();
        }

        SDL_SetCursor( g_imgui_to_sdl_cursor[ImGui::GetMouseCursor()] );

        Moonlight::CameraData cam;
        FrameRenderData frameRenderData;
        m_renderer->Render( cam, frameRenderData );
        m_input.PostUpdate();
    }
}

void Tool::Shutdown()
{
    if( !m_renderer )
    {
        return;
    }
    if( ImGui::GetCurrentContext() && ImGui::GetIO().BackendPlatformUserData )
    {
        ImGui_ImplSDL2_Shutdown();
    }
    m_renderer->Destroy();
    delete m_renderer;
    m_renderer = nullptr;
    for( SDL_Cursor*& cursor : g_imgui_to_sdl_cursor )
    {
        if( cursor )
        {
            SDL_FreeCursor( cursor );
            cursor = nullptr;
        }
    }
    delete m_window;
    m_window = nullptr;
}
