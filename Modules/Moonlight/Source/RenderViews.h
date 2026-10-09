#pragma once
#include <bgfx/bgfx.h>
#include <cstdint>

// Every bgfx view ID the engine uses, in one place.
//
// bgfx executes views in ascending ID order, and blits queued on a view run
// *before* that view's draws. So any view that samples another view's output
// must have a higher ID than the producer, or it reads last frame's result.
//
// Frame order:
//   Clear -> Ultralight buffers -> UI resolve -> per-camera passes (dynamic) -> picking
//   -> UI composite -> ImGui
namespace Moonlight::RenderView
{
    // Backbuffer clear.
    constexpr bgfx::ViewId Clear = 0;

    // Ultralight GPUDriver render buffers, one view per render buffer ID.
    constexpr bgfx::ViewId UIDriverFirst = 1;
    constexpr bgfx::ViewId UIDriverCount = 32;

    // Resolves the Ultralight view render target into the UI texture.
    constexpr bgfx::ViewId UIResolve = UIDriverFirst + UIDriverCount;

    // Allocated per frame in submission order (ViewAllocator): environment/IBL updates, then each
    // camera's shadow, scene and post-processing passes. The main camera renders last so it can
    // sample render-to-texture cameras from this frame.
    constexpr bgfx::ViewId DynamicFirst = UIResolve + 1;
    constexpr bgfx::ViewId DynamicLast = 223;

    // Editor picking: ID pass, then blit to the CPU readback texture.
    constexpr bgfx::ViewId PickingId = DynamicLast + 1;
    constexpr bgfx::ViewId PickingBlit = PickingId + 1;

    // Composites the UI texture over the main camera's output.
    constexpr bgfx::ViewId UIComposite = PickingBlit + 1;

    // ImGui platform windows count down from ImGuiPlatformLast; the main ImGui pass is last.
    constexpr bgfx::ViewId ImGuiPlatformLast = 254;
    constexpr bgfx::ViewId ImGuiMain = 255;

    static_assert( UIComposite < ImGuiPlatformLast, "Render view bands overlap the ImGui band." );
}

namespace Moonlight
{
    // Hands out view IDs from the dynamic band in submission order, naming them for debuggers.
    class ViewAllocator
    {
    public:
        void Reset() { m_next = RenderView::DynamicFirst; m_exhausted = false; }

        // Returns UINT16_MAX when the band is exhausted (the pass should be skipped).
        bgfx::ViewId Allocate( const char* InName );

        uint32_t GetUsedCount() const { return m_next - RenderView::DynamicFirst; }
        bool IsExhausted() const { return m_exhausted; }

    private:
        uint32_t m_next = RenderView::DynamicFirst;
        bool m_exhausted = false;
    };
}
