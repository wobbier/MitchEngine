#pragma once
#include "Dementia.h"
#if USING( ME_EDITOR )
#include "Math/Vector2.h"
#include <array>
#include <string>
#include <vector>

class SceneViewWidget;

// The editor's side of the colour popover (UX-14, MeWidgets::ColorSwatchPopover): the scene
// palette and the World View eyedropper.
//   Scene palette: every reflected colour field (FieldFlags::Color) of every component in the open
//   world, HDR colours by their displayable base; the popover merges and ranks them.
//   Eyedropper: the popover's Eyedropper button arms it for that swatch; the next left click in
//   the World View copies the pixel under the pointer back from the GPU (the image the view shows,
//   so after lighting, exposure and tonemapping) and, a few frames later when the copy lands, the
//   swatch writes it to its field (one undo step, through the inspector). Esc cancels. While armed
//   the view shows a crosshair and a loupe and the click selects nothing.
namespace ColorPicking
{
    // Installs the popover hooks (Havana startup).
    void Install();

    bool IsArmed();
    void Cancel();

    // Called by the World View each frame with the image's rect (ImGui coordinates): true while
    // the eyedropper owns the pointer (armed, or a sample waiting to start).
    bool DrawViewport( SceneViewWidget& InView, const Vector2& InImageMin, const Vector2& InImageSize );

    // Automation (assert-viewport-pixel): the World View's displayed colour (sRGB 0..1) at a
    // fraction of its image. The copy takes a few frames: OutRetry until it has landed.
    bool ReadViewportPixel( SceneViewWidget& InView, float InX, float InY, std::array<float, 3>& OutRgb, std::string& OutError, bool& OutRetry );

    // The colours reflected colour fields hold in the open world, with repeats.
    std::vector<std::array<float, 3>> GatherSceneColors();
}
#endif
