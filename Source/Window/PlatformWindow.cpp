#include "PCH.h"
#include "PlatformWindow.h"

#include <SDL.h>

#if USING( ME_PLATFORM_LINUX )
#define KeyCode X11KeyCode
#define Window  X11Window

#ifndef SDL_VIDEO_DRIVER_WAYLAND
#define SDL_VIDEO_DRIVER_WAYLAND 1
#endif

#ifndef SDL_VIDEO_DRIVER_X11
#define SDL_VIDEO_DRIVER_X11 1
#endif
#endif

#include <SDL_syswm.h>

#if USING( ME_PLATFORM_LINUX )
#undef KeyCode
#undef Window
#undef None
#endif

#include <ImGui/ImGuiRenderer.h>
#include <imgui.h>

PlatformWindow::PlatformWindow( PlatformWindowParams& InParams )
    : Params( InParams )
    , Buffer( BGFX_INVALID_HANDLE )
{
}

void PlatformWindow::Create()
{
    WindowPtr = SDL_CreateWindow( Params.Name.c_str(), Params.Position.x, Params.Position.y, Params.Size.x, Params.Size.y, Params.Flags );

    if( WindowPtr == nullptr ) {
        printf( "WindowBase could not be created. SDL_Error: %s\n", SDL_GetError() );
        return;
    }
    SetWindow( WindowPtr );
    Reset();
    //SetWindowId(SDL_GetWindowID(WindowPtr));
    //MapWindow(GetWindowId(), this);
}

void PlatformWindow::Destroy()
{
#if USING( ME_PLATFORM_LINUX )
    if (GLContext)
    {
        SDL_GL_DeleteContext(GLContext);
        GLContext = nullptr;
    }
#endif
    if( bgfx::isValid( Buffer ) )
    {
        bgfx::destroy( Buffer );
    }
    //UnmapWindow(GetWindowId());
    SDL_DestroyWindow( WindowPtr );
    WindowPtr = nullptr;
}

void PlatformWindow::SetWindow( SDL_Window* window )
{
    SDL_SysWMinfo wmi;

    SDL_VERSION( &wmi.version );
    if( !SDL_GetWindowWMInfo( window, &wmi ) )
    {
        printf(
            "SDL_SysWMinfo could not be retrieved. SDL_Error: %s\n",
            SDL_GetError() );
    }

#if USING( ME_PLATFORM_WIN64 )
    PlatformInfo.ndt = nullptr;
    PlatformInfo.nwh = wmi.info.win.window;
#endif
#if USING( ME_PLATFORM_MACOS )
    PlatformInfo.ndt = nullptr;
    PlatformInfo.nwh = wmi.info.cocoa.window;
#endif
#if USING( ME_PLATFORM_LINUX )
    switch (wmi.subsystem)
    {
        case SDL_SYSWM_WAYLAND:
            // Wayland: ndt = wl_display*, nwh = wl_surface*
            PlatformInfo.ndt = wmi.info.wl.display;
            PlatformInfo.nwh = (void*)wmi.info.wl.egl_window;
            break;

        case SDL_SYSWM_X11:
            // X11: ndt = Display*, nwh = Window
            PlatformInfo.ndt = wmi.info.x11.display;
            PlatformInfo.nwh = (void*)(uintptr_t)wmi.info.x11.window;
            break;

        default:
            printf("Unhandled SDL WM subsystem: %d\n", (int)wmi.subsystem);
            break;
    }
#endif
    PlatformInfo.context = nullptr;
    PlatformInfo.backBuffer = nullptr;
    PlatformInfo.backBufferDS = nullptr;
}

void PlatformWindow::Render()
{
    Renderer->Render( Viewport->DrawData, GetViewId() );
}

void PlatformWindow::Reset( bool soft )
{
    if( soft )
    {
        if( bgfx::isValid( Buffer ) )
        {
            bgfx::setViewFrameBuffer( GetViewId(), Buffer );
        }
        return;
    }

    if( bgfx::isValid( Buffer ) )
    {
        bgfx::destroy( Buffer );
    }

    Buffer = bgfx::createFrameBuffer( GetHWND(), GetSize().x, GetSize().y );
    bgfx::setViewFrameBuffer( GetViewId(), Buffer );
}

void* PlatformWindow::GetHWND()
{
    return PlatformInfo.nwh;
}

void PlatformWindow::SetSize( const Vector2& InSize )
{
    SDL_SetWindowSize( WindowPtr, InSize.x, InSize.y );
}

Vector2 PlatformWindow::GetSize() const
{
    int x = 0, y = 0;
    SDL_GetWindowSize( WindowPtr, &x, &y );
    return { x, y };
}

Vector2 PlatformWindow::GetClientSize()
{
    int clientWidth, clientHeight;
    SDL_GL_GetDrawableSize( WindowPtr, &clientWidth, &clientHeight );
    return { clientWidth, clientHeight };
}

void PlatformWindow::SetPosition( const Vector2& InPosition )
{
    SDL_SetWindowPosition( WindowPtr, InPosition.x, InPosition.y );
    Reset();
}

Vector2 PlatformWindow::GetPosition()
{
    int x = 0, y = 0;
    SDL_GetWindowPosition( WindowPtr, &x, &y );
    return { x, y };
}

Vector2 PlatformWindow::GetClientPosition()
{
    //#TODO: Return the client position
    return GetPosition();
}

void PlatformWindow::Show()
{
    SDL_ShowWindow( WindowPtr );
    Reset();
}

void PlatformWindow::CreateAndShow()
{
    Create();
    Show();
}
