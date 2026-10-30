#pragma once
#include "Dementia.h"

#if USING( ME_EDITOR )

#include "Math/Vector2.h"

class SceneViewWidget;

// The World view's spline handles (GAME-2): with one Spline selected, its curve is drawn with a
// handle on each point. Dragging a handle moves that point in the plane facing the camera, one
// undo step per drag.
namespace SplineGizmo
{
    // Called by the World view each frame; true while the gizmo owns the mouse.
    bool DrawViewport( SceneViewWidget& InView, const Vector2& InScreenMin, const Vector2& InScreenSize );
    // Where point InIndex of the selected spline is on screen (edscript spline-handle-mouse).
    bool GetHandleScreenPosition( SceneViewWidget& InView, int InIndex, Vector2& OutPosition );
}

#endif
