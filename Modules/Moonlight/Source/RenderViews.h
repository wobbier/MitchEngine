#pragma once
#include <bgfx/bgfx.h>
#include <cstdint>
#include <string>
#include <vector>

// Every bgfx view ID the engine uses, in one place.
//
// bgfx executes views in ascending ID order, and blits queued on a view run
// *before* that view's draws. So any view that samples another view's output
// must have a higher ID than the producer, or it reads last frame's result.
//
// Frame order:
//   Clear -> game UI (RmlUi) -> per-camera passes (dynamic) -> picking
//   -> UI composite -> editor offscreen (previews) -> ImGui
namespace Moonlight::RenderView
{
    // Backbuffer clear.
    constexpr bgfx::ViewId Clear = 0;

    // Game UI: RmlUi documents render into their offscreen target here, before anything that
    // samples it (the composite). The rest of the band is headroom for UI layers/filters.
    constexpr bgfx::ViewId UIFirst = 1;
    constexpr bgfx::ViewId UICount = 8;
    constexpr bgfx::ViewId RmlUi = UIFirst;

    // Allocated per frame in submission order (ViewAllocator): environment/IBL updates, then each
    // camera's shadow, scene and post-processing passes. The main camera renders last so it can
    // sample render-to-texture cameras from this frame.
    constexpr bgfx::ViewId DynamicFirst = UIFirst + UICount;
    constexpr bgfx::ViewId DynamicLast = 223;

    // Editor picking: ID pass, then blit to the CPU readback texture.
    constexpr bgfx::ViewId PickingId = DynamicLast + 1;
    constexpr bgfx::ViewId PickingBlit = PickingId + 1;

    // Composites the game UI texture over the main camera's output.
    constexpr bgfx::ViewId UIComposite = PickingBlit + 1;

    // Editor offscreen work submitted from the UI (shader graph node previews), sampled by ImGui.
    constexpr bgfx::ViewId EditorOffscreenFirst = UIComposite + 1;
    constexpr bgfx::ViewId EditorOffscreenCount = 16;
    // The browser inspector has its own preview, including while graph documents are open.
    constexpr bgfx::ViewId AssetPreviewTiles = EditorOffscreenFirst + EditorOffscreenCount - 3;
    constexpr bgfx::ViewId AssetPreviewMain = AssetPreviewTiles + 1;
    // Copies editor preview images to the CPU (after the cameras that render them).
    constexpr bgfx::ViewId EditorCapture = AssetPreviewMain + 1;

    // Stable IDs for detached ImGui windows; the main ImGui pass is last.
    constexpr bgfx::ViewId ImGuiPlatformFirst = EditorOffscreenFirst + EditorOffscreenCount;
    constexpr bgfx::ViewId ImGuiPlatformLast = 254;
    constexpr bgfx::ViewId ImGuiMain = 255;

    static_assert( ImGuiPlatformFirst <= ImGuiPlatformLast - 8, "Render view bands overlap the ImGui band." );
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
        // This frame's name for a dynamic view (empty for fixed or unused ids).
        const std::string& GetName( bgfx::ViewId InId ) const;

    private:
        uint32_t m_next = RenderView::DynamicFirst;
        bool m_exhausted = false;
        std::vector<std::string> m_names;
    };
}
