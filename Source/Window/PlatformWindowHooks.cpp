#include "PCH.h"
#include "PlatformWindowHooks.h"
#include <ImGui/ImGuiRenderer.h>
#include <SDL.h>
#include <imgui.h>
#include <array>

#if USING( ME_PLATFORM_LINUX )
// SDL_syswm includes Xlib, whose names collide with engine and ImGui types.
#define KeyCode X11KeyCode
#define Window X11Window
#ifndef SDL_VIDEO_DRIVER_X11
#define SDL_VIDEO_DRIVER_X11 1
#endif
#include <SDL_syswm.h>
#undef KeyCode
#undef Window
#undef None
#endif

namespace
{
    std::array<bool, Moonlight::RenderView::ImGuiPlatformLast - Moonlight::RenderView::ImGuiPlatformFirst + 1> ViewIdsInUse = {};

    struct ViewportRenderData
    {
        bgfx::FrameBufferHandle Buffer = BGFX_INVALID_HANDLE;
        bgfx::ViewId ViewId = UINT16_MAX;
        int Width = 0;
        int Height = 0;
    };

    void ResizeRenderTarget( ImGuiViewport* viewport, ImVec2 )
    {
        auto* data = static_cast<ViewportRenderData*>( viewport->RendererUserData );
        if( !data || data->ViewId == UINT16_MAX )
            return;

        auto* window = static_cast<SDL_Window*>( viewport->PlatformHandle );
        int width = 0, height = 0;
        SDL_GetWindowSizeInPixels( window, &width, &height );
        if( width <= 0 || height <= 0 || ( data->Width == width && data->Height == height ) )
            return;

        bgfx::SwapChain swapChain;
        swapChain.width = static_cast<uint32_t>( width );
        swapChain.height = static_cast<uint32_t>( height );
        data->Width = width;
        data->Height = height;
        if( bgfx::isValid( data->Buffer ) )
        {
            // Resizing in place keeps the handle and avoids a GPU wait to release the old swap chain.
            bgfx::updateSwapChain( data->Buffer, swapChain );
            return;
        }

        void* nativeWindow = viewport->PlatformHandleRaw;
#if USING( ME_PLATFORM_LINUX )
        SDL_SysWMinfo info = {};
        SDL_VERSION( &info.version );
        if( SDL_GetWindowWMInfo( window, &info ) && info.subsystem == SDL_SYSWM_X11 )
            nativeWindow = reinterpret_cast<void*>( static_cast<uintptr_t>( info.info.x11.window ) );
#endif
        if( !nativeWindow )
        {
            data->Width = data->Height = 0;
            return;
        }

        swapChain.nwh = nativeWindow;
        data->Buffer = bgfx::createFrameBuffer( swapChain );
        bgfx::setViewFrameBuffer( data->ViewId, data->Buffer );
    }

    void CreateRenderTarget( ImGuiViewport* viewport )
    {
        auto* data = new ViewportRenderData();
        viewport->RendererUserData = data;

        // A viewport keeps its ID until destruction, including while minimized. Never borrow
        // views from camera passes or shader previews when more windows are opened.
        // Track ownership independently of PlatformIO.Viewports, which only lists active windows.
        for( size_t index = 0; index < ViewIdsInUse.size(); ++index )
        {
            if( !ViewIdsInUse[index] )
            {
                ViewIdsInUse[index] = true;
                data->ViewId = static_cast<bgfx::ViewId>( Moonlight::RenderView::ImGuiPlatformFirst + index );
                break;
            }
        }
        if( data->ViewId == UINT16_MAX )
        {
            SDL_LogError( SDL_LOG_CATEGORY_RENDER, "ImGui platform window render views exhausted." );
            return;
        }
        ResizeRenderTarget( viewport, viewport->Size );
    }

    void DestroyRenderTarget( ImGuiViewport* viewport )
    {
        auto* data = static_cast<ViewportRenderData*>( viewport->RendererUserData );
        if( !data )
            return;
        if( data->ViewId != UINT16_MAX )
            bgfx::resetView( data->ViewId );
        if( bgfx::isValid( data->Buffer ) )
        {
            bgfx::destroy( data->Buffer );
            // bgfx defers resource destruction. Drain both command frames before SDL destroys the
            // native window backing this swap chain (the platform callback runs immediately next).
            bgfx::frame();
            bgfx::frame();
        }
        if( data->ViewId != UINT16_MAX )
            ViewIdsInUse[data->ViewId - Moonlight::RenderView::ImGuiPlatformFirst] = false;
        delete data;
        viewport->RendererUserData = nullptr;
    }

    void RenderWindow( ImGuiViewport* viewport, void* )
    {
        // Check drawable size here too: moving between monitors can change the pixel density.
        ResizeRenderTarget( viewport, viewport->Size );
        auto* data = static_cast<ViewportRenderData*>( viewport->RendererUserData );
        if( !data || !bgfx::isValid( data->Buffer ) || !viewport->DrawData || viewport->Size.x <= 0 || viewport->Size.y <= 0 )
            return;

        viewport->DrawData->FramebufferScale = ImVec2( data->Width / viewport->Size.x, data->Height / viewport->Size.y );
        bgfx::setViewFrameBuffer( data->ViewId, data->Buffer );
        bgfx::setViewClear( data->ViewId, BGFX_CLEAR_COLOR, 0x000000ff );
        bgfx::touch( data->ViewId );
        auto* renderer = static_cast<ImGuiRenderer*>( ImGui::GetIO().BackendRendererUserData );
        renderer->Render( viewport->DrawData, data->ViewId );
    }

#if USING( ME_PLATFORM_LINUX )
    void ( *SDLCreateWindow )( ImGuiViewport* ) = nullptr;

    void CreateResizableWindow( ImGuiViewport* viewport )
    {
        // SDL creates undecorated windows non-resizable, which pins their min and max size hints
        // to the current size, so window managers (Hyprland, i3) refuse border resizes. ImGui
        // follows a resize made by the window manager through SDL's resize event.
        SDLCreateWindow( viewport );
        SDL_SetWindowResizable( static_cast<SDL_Window*>( viewport->PlatformHandle ), SDL_TRUE );
    }
#endif
}

void ImGui::InitHooks()
{
    ImGuiIO& io = ImGui::GetIO();
    ImGuiPlatformIO& platformIO = ImGui::GetPlatformIO();
    if( !( io.BackendFlags & ImGuiBackendFlags_PlatformHasViewports ) || !( io.BackendFlags & ImGuiBackendFlags_RendererHasViewports ) || !platformIO.Platform_CreateWindow )
    {
        // For example, SDL's Wayland backend cannot supply the global mouse coordinates ImGui needs.
        io.ConfigFlags &= ~ImGuiConfigFlags_ViewportsEnable;
        return;
    }

    // SDL owns PlatformUserData and all native window/input callbacks. Only install the bgfx
    // renderer half of the interface, so SDL's focus, close, IME and Windows tool-window handling work.
    platformIO.Renderer_CreateWindow = CreateRenderTarget;
    platformIO.Renderer_DestroyWindow = DestroyRenderTarget;
    platformIO.Renderer_SetWindowSize = ResizeRenderTarget;
    platformIO.Renderer_RenderWindow = RenderWindow;
#if USING( ME_PLATFORM_LINUX )
    SDLCreateWindow = platformIO.Platform_CreateWindow;
    platformIO.Platform_CreateWindow = CreateResizableWindow;
#endif
}
