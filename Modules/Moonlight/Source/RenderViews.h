#pragma once
#include <bgfx/bgfx.h>

// Every bgfx view ID the engine uses, in one place.
//
// bgfx executes views in ascending ID order, and blits queued on a view run
// *before* that view's draws. So any view that samples another view's output
// must have a higher ID than the producer, or it reads last frame's result.
//
// Frame order:
//   Clear -> Ultralight buffers -> UI resolve -> cameras -> picking -> UI composite -> ImGui
namespace Moonlight::RenderView
{
    // Backbuffer clear.
    constexpr bgfx::ViewId Clear = 0;

    // Ultralight GPUDriver render buffers, one view per render buffer ID.
    constexpr bgfx::ViewId UIDriverFirst = 1;
    constexpr bgfx::ViewId UIDriverCount = 32;

    // Resolves the Ultralight view render target into the UI texture.
    constexpr bgfx::ViewId UIResolve = UIDriverFirst + UIDriverCount;

    // Scene cameras. The main camera is always rendered last so it can sample
    // render-to-texture cameras from this frame.
    constexpr bgfx::ViewId CameraFirst = UIResolve + 1;
    constexpr bgfx::ViewId CameraCount = 64;
    constexpr bgfx::ViewId CameraLast = CameraFirst + CameraCount - 1;

    // Editor picking: ID pass, then blit to the CPU readback texture.
    constexpr bgfx::ViewId PickingId = CameraLast + 1;
    constexpr bgfx::ViewId PickingBlit = PickingId + 1;

    // Composites the UI texture over the main camera's output.
    constexpr bgfx::ViewId UIComposite = PickingBlit + 1;

    // ImGui platform windows count down from ImGuiPlatformLast; the main ImGui pass is last.
    constexpr bgfx::ViewId ImGuiPlatformLast = 254;
    constexpr bgfx::ViewId ImGuiMain = 255;

    static_assert( UIComposite < ImGuiPlatformLast, "Render view bands overlap the ImGui band." );
}
