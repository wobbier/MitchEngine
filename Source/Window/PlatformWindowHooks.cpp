#include "PCH.h"
#include <Window/SDLWindow.h>
#include <Window/PlatformWindow.h>
#include <imgui.h>

#define SDL_HAS_CAPTURE_AND_GLOBAL_MOUSE    (SDL_VERSION_ATLEAST(2,0,4) && !defined(__EMSCRIPTEN__) && !defined(__ANDROID__) && !(defined(__APPLE__) && TARGET_OS_IOS))
#define SDL_HAS_MOUSE_FOCUS_CLICKTHROUGH    SDL_VERSION_ATLEAST(2,0,5)
#define SDL_HAS_WINDOW_ALPHA                SDL_VERSION_ATLEAST(2,0,5)
#define SDL_HAS_ALWAYS_ON_TOP               SDL_VERSION_ATLEAST(2,0,5)
#define SDL_HAS_USABLE_DISPLAY_BOUNDS       SDL_VERSION_ATLEAST(2,0,5)
#define SDL_HAS_PER_MONITOR_DPI             SDL_VERSION_ATLEAST(2,0,4)
#define SDL_HAS_VULKAN                      SDL_VERSION_ATLEAST(2,0,6)

static void Platform_CreateWindow( ImGuiViewport* viewport )
{
    ImGuiIO& io = ImGui::GetIO();
    //MitchHub& gui = *(MitchHub*)io.BackendPlatformUserData;

    ImGuiViewport* main_viewport = ImGui::GetMainViewport();
    PlatformWindow& app = *(PlatformWindow*)main_viewport->PlatformUserData;

    uint32_t sdl_flags = 0;
    //sdl_flags |= SDL_GetWindowFlags(app.m_window->GetWindowPtr()) & SDL_WINDOW_ALLOW_HIGHDPI;
    sdl_flags |= SDL_WINDOW_HIDDEN;
    sdl_flags |= ( viewport->Flags & ImGuiViewportFlags_NoDecoration ) ? SDL_WINDOW_BORDERLESS : 0;
    sdl_flags |= ( viewport->Flags & ImGuiViewportFlags_NoDecoration ) ? 0 : SDL_WINDOW_RESIZABLE;
#if !defined(_WIN32)
    // See SDL hack in Platform_ShowWindow().
    sdl_flags |= ( viewport->Flags & ImGuiViewportFlags_NoTaskBarIcon ) ? SDL_WINDOW_SKIP_TASKBAR : 0;
#endif
#if SDL_HAS_ALWAYS_ON_TOP
    sdl_flags |= ( viewport->Flags & ImGuiViewportFlags_TopMost ) ? SDL_WINDOW_ALWAYS_ON_TOP : 0;
#endif
    PlatformWindowParams params;
    params.Name = "No Title Yet";
    params.Flags = sdl_flags;
    params.Position = { viewport->Pos.x, viewport->Pos.y };
    params.Size = { viewport->Size.x, viewport->Size.y };

    PlatformWindow* vp = new PlatformWindow( params );
    vp->Create();
    vp->Viewport = viewport;
    vp->Renderer = app.Renderer;

    viewport->PlatformUserData = vp;
    viewport->PlatformHandle = (void*)vp->WindowPtr;
    viewport->PlatformHandleRaw = vp->GetHWND();

}

static void Platform_DestroyWindow( ImGuiViewport* viewport )
{
    if( viewport != ImGui::GetMainViewport() )
    {
        PlatformWindow* app = (PlatformWindow*)viewport->PlatformUserData;
        app->Destroy();

        delete app;
    }
    viewport->PlatformUserData = nullptr;
    viewport->PlatformHandle = nullptr;
}

static void Platform_ShowWindow( ImGuiViewport* viewport )
{
    PlatformWindow& app = *(PlatformWindow*)viewport->PlatformUserData;
    app.Show();
}

static ImVec2 Platform_GetWindowPos( ImGuiViewport* viewport )
{
    PlatformWindow& app = *(PlatformWindow*)viewport->PlatformUserData;
    Vector2 pos = app.GetPosition();
    return { pos.x, pos.y };
}

static void Platform_SetWindowPos( ImGuiViewport* viewport, ImVec2 pos )
{
    PlatformWindow& app = *(PlatformWindow*)viewport->PlatformUserData;
    app.SetPosition( { pos.x, pos.y } );
}

static ImVec2 Platform_GetWindowSize( ImGuiViewport* viewport )
{
    PlatformWindow& app = *(PlatformWindow*)viewport->PlatformUserData;
    Vector2 size = app.GetSize();
    return { size.x, size.y };
}

static void Platform_SetWindowSize( ImGuiViewport* viewport, ImVec2 size )
{
    PlatformWindow& app = *(PlatformWindow*)viewport->PlatformUserData;
    app.SetSize( { size.x, size.y } );
    app.Reset();
}

static void Platform_SetWindowTitle( ImGuiViewport* viewport, const char* title )
{
    PlatformWindow& wnd = *(PlatformWindow*)viewport->PlatformUserData;
    SDL_SetWindowTitle( wnd.WindowPtr, title );
}

static bool Platform_GetWindowFocus( ImGuiViewport* viewport )
{
    PlatformWindow& wnd = *(PlatformWindow*)viewport->PlatformUserData;
    return ( SDL_GetWindowFlags( wnd.WindowPtr ) & SDL_WINDOW_INPUT_FOCUS ) != 0;
}

static void Platform_SetWindowFocus( ImGuiViewport* viewport )
{
    PlatformWindow& wnd = *(PlatformWindow*)viewport->PlatformUserData;
    SDL_RaiseWindow( wnd.WindowPtr );
}

static bool Platform_GetWindowMinimized( ImGuiViewport* viewport )
{
    if( !viewport->DrawData )
    {
        return false;
    }
    PlatformWindow& wnd = *(PlatformWindow*)viewport->PlatformUserData;
    return ( SDL_GetWindowFlags( wnd.WindowPtr ) & SDL_WINDOW_MINIMIZED ) != 0;
}

static void Platform_RenderWindow( ImGuiViewport* viewport, void* platformIndex )
{
    PlatformWindow& wnd = *(PlatformWindow*)viewport->PlatformUserData;
    uint16_t layer = *(uint16_t*)platformIndex;
    if( wnd.ViewId != layer )
    {
        wnd.ViewId = layer;
        wnd.Reset( true );
    }
    wnd.Render();
}

#if SDL_HAS_WINDOW_ALPHA
static void Platform_SetWindowAlpha( ImGuiViewport* viewport, float alpha )
{
}
#endif

namespace ImGui
{
    void InitHooks( SDLWindow* window, ImGuiRenderer* renderer )
    {
        ImGuiPlatformIO& io = ImGui::GetPlatformIO();
        io.Platform_CreateWindow = Platform_CreateWindow;
        io.Platform_DestroyWindow = Platform_DestroyWindow;
        io.Platform_ShowWindow = Platform_ShowWindow;
        io.Platform_SetWindowPos = Platform_SetWindowPos;
        io.Platform_GetWindowPos = Platform_GetWindowPos;
        io.Platform_SetWindowSize = Platform_SetWindowSize;
        io.Platform_GetWindowSize = Platform_GetWindowSize;
        io.Platform_SetWindowFocus = Platform_SetWindowFocus;
        io.Platform_GetWindowFocus = Platform_GetWindowFocus;
        io.Platform_GetWindowMinimized = Platform_GetWindowMinimized;
        io.Platform_SetWindowTitle = Platform_SetWindowTitle;
        io.Platform_RenderWindow = Platform_RenderWindow;
#if SDL_HAS_WINDOW_ALPHA
        io.Platform_SetWindowAlpha = Platform_SetWindowAlpha;
#endif

        // Asserts on start are because of the classes being built different,
        // move stuff to IWindow 
        // viewport->PlatformUserData is MitchHub here, but is expected as a PlatformWindow in the callbacks
        PlatformWindowParams p;
        p.Name = "LMAO";
        p.Size = { 1920, 1080 };

        PlatformWindow* win = new PlatformWindow( p );
        win->WindowPtr = window->WindowHandle;
        win->Renderer = renderer;

        ImGuiViewport* viewport = ImGui::GetMainViewport();
        viewport->PlatformUserData = win;
        viewport->PlatformHandle = window->WindowHandle;
        viewport->PlatformHandleRaw = window->GetWindowPtr();
    }
}