#include "Tool.h"

#include <functional>

#include "SDL_video.h"

#include "Renderer.h"
#include "Math/Vector2.h"
#include "Window/SDLWindow.h"
#include "imgui.h"
#include "UI/Colors.h"
#include "Window/PlatformWindowHooks.h"
#include <Camera/CameraData.h>

extern bool ImGui_ImplSDL2_InitForMetal( SDL_Window* window );
extern bool ImGui_ImplSDL2_InitForD3D( SDL_Window* window );

Tool::Tool( ToolCreationFlags& inToolCreationFlags )
    : m_toolCreationFlags( inToolCreationFlags )
{
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

    m_window = new SDLWindow( "ME HUB", ResizeFunc, 500, 300, Vector2( 1280, 720 ) );
    m_window->SetBorderless( m_toolCreationFlags.isBorderless );
    ResizeFunc( Vector2( 1280, 720 ) );
    RendererCreationSettings set;
    set.WindowPtr = m_window->GetWindowPtr();
    set.InitAssets = false;
    m_renderer->Create( set );

#if USING( ME_PLATFORM_WIN64 )
    ImGui_ImplSDL2_InitForD3D( static_cast<SDLWindow*>( m_window )->WindowHandle );
#endif
#if USING( ME_PLATFORM_MACOS )
    ImGui_ImplSDL2_InitForMetal( static_cast<SDLWindow*>( m_window )->WindowHandle );
#endif


    ImGuiIO& io = ImGui::GetIO(); (void)io;
    Path EngineConfigFilePath = Path( ".tmp/hub-imgui.cfg" );
    io.IniFilename = EngineConfigFilePath.FullPath.c_str();

    io.BackendFlags |= ImGuiBackendFlags_PlatformHasViewports | ImGuiBackendFlags_RendererHasViewports;

    {
        ImVec4* colors = ImGui::GetStyle().Colors;
        colors[ImGuiCol_Text] = COLOR_TEXT;
        colors[ImGuiCol_TextDisabled] = ImVec4( 0.50f, 0.50f, 0.50f, 1.00f );
        colors[ImGuiCol_WindowBg] = COLOR_FOREGROUND;
        colors[ImGuiCol_ChildBg] = ImVec4( 1.00f, 1.00f, 1.00f, 0.00f );
        colors[ImGuiCol_PopupBg] = ImVec4( 0.08f, 0.08f, 0.08f, 0.94f );
        colors[ImGuiCol_Border] = ImVec4( 0.43f, 0.43f, 0.50f, 0.50f );
        colors[ImGuiCol_BorderShadow] = ImVec4( 0.00f, 0.00f, 0.00f, 0.00f );
        colors[ImGuiCol_FrameBg] = COLOR_DROPDOWN;
        colors[ImGuiCol_FrameBgHovered] = ImVec4( 0.40f, 0.40f, 0.40f, 0.40f );
        colors[ImGuiCol_FrameBgActive] = ImVec4( 0.18f, 0.18f, 0.18f, 0.67f );
        colors[ImGuiCol_TitleBg] = COLOR_BACKGROUND_BORDER;
        colors[ImGuiCol_TitleBgActive] = COLOR_BACKGROUND_BORDER;
        colors[ImGuiCol_TitleBgCollapsed] = ImVec4( 0.00f, 0.00f, 0.00f, 0.51f );
        colors[ImGuiCol_MenuBarBg] = COLOR_FOREGROUND;
        colors[ImGuiCol_ScrollbarBg] = COLOR_FOREGROUND;
        colors[ImGuiCol_ScrollbarGrab] = COLOR_WHITE_25;
        colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4( 0.41f, 0.41f, 0.41f, 1.00f );
        colors[ImGuiCol_ScrollbarGrabActive] = ImVec4( 0.51f, 0.51f, 0.51f, 1.00f );
        colors[ImGuiCol_CheckMark] = ImVec4( 0.94f, 0.94f, 0.94f, 1.00f );
        colors[ImGuiCol_SliderGrab] = ImVec4( 0.51f, 0.51f, 0.51f, 1.00f );
        colors[ImGuiCol_SliderGrabActive] = ImVec4( 0.86f, 0.86f, 0.86f, 1.00f );
        colors[ImGuiCol_Button] = { 0.220f, 0.220f, 0.220f, 0.25f };
        colors[ImGuiCol_ButtonHovered] = COLOR_PRIMARY_HOVER;
        colors[ImGuiCol_ButtonActive] = COLOR_PRIMARY_PRESS;
        colors[ImGuiCol_Header] = COLOR_WHITE_25;
        colors[ImGuiCol_HeaderHovered] = COLOR_PRIMARY_HOVER;
        colors[ImGuiCol_HeaderActive] = COLOR_PRIMARY_PRESS;
        colors[ImGuiCol_Separator] = COLOR_BACKGROUND_BORDER;
        colors[ImGuiCol_SeparatorHovered] = COLOR_PRIMARY_HOVER;
        colors[ImGuiCol_SeparatorActive] = COLOR_PRIMARY_PRESS;
        colors[ImGuiCol_ResizeGrip] = ImVec4( 0.91f, 0.91f, 0.91f, 0.25f );
        colors[ImGuiCol_ResizeGripHovered] = ImVec4( 0.81f, 0.81f, 0.81f, 0.67f );
        colors[ImGuiCol_ResizeGripActive] = ImVec4( 0.46f, 0.46f, 0.46f, 0.95f );
        colors[ImGuiCol_PlotLines] = ImVec4( 0.61f, 0.61f, 0.61f, 1.00f );
        colors[ImGuiCol_PlotLinesHovered] = ImVec4( 1.00f, 0.43f, 0.35f, 1.00f );
        colors[ImGuiCol_PlotHistogram] = ImVec4( 0.73f, 0.60f, 0.15f, 1.00f );
        colors[ImGuiCol_PlotHistogramHovered] = ImVec4( 1.00f, 0.60f, 0.00f, 1.00f );
        colors[ImGuiCol_TextSelectedBg] = ImVec4( 0.87f, 0.87f, 0.87f, 0.35f );
        //colors[ImGuiCol_ModalWindowDarkening] = ImVec4(0.80f, 0.80f, 0.80f, 0.35f);
        colors[ImGuiCol_DragDropTarget] = ImVec4( 1.00f, 1.00f, 0.00f, 0.90f );
        colors[ImGuiCol_NavHighlight] = ImVec4( 0.60f, 0.60f, 0.60f, 1.00f );
        colors[ImGuiCol_NavWindowingHighlight] = ImVec4( 1.00f, 1.00f, 1.00f, 0.70f );
        colors[ImGuiCol_Tab] = COLOR_FOREGROUND;
        colors[ImGuiCol_TabHovered] = COLOR_PRIMARY_HOVER;
        colors[ImGuiCol_TabActive] = COLOR_PRIMARY;
        colors[ImGuiCol_TabUnfocused] = COLOR_TITLE;
        colors[ImGuiCol_TabUnfocusedActive] = COLOR_FOREGROUND;
        colors[ImGuiCol_TableHeaderBg] = COLOR_HEADER;
        colors[ImGuiCol_TableRowBg] = COLOR_TITLE;
        colors[ImGuiCol_TableRowBgAlt] = COLOR_RECESSED;
    }

    // Hooking into ImGui
    {
        ImGui::InitHooks( m_window, m_renderer->GetImGuiRenderer() );
    }

    /*{
        ProjectEntry p;
        p.BackgroundImage = ResourceCache::GetInstance().Get<Moonlight::Texture>(Path("fl.png"));
        p.TitleImage = logo;
        p.Name = "Fruit Loops";
        Cache.Projects.push_back(p);
    }

    {
        ProjectEntry p;
        p.BackgroundImage = ResourceCache::GetInstance().Get<Moonlight::Texture>(Path("run.png"));
        p.TitleImage = logo;
        p.Name = "Fruit Loops: Secret Department";
        Cache.Projects.push_back(p);
    }*/

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
}

void Tool::Run()
{
    while( !m_window->ShouldClose() )
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
            , 255 );

        OnUpdate();

        Moonlight::CameraData cam;
        m_renderer->Render( cam );
        m_input.PostUpdate();
    }
}

