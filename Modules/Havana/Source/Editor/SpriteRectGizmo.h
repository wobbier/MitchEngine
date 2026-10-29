#pragma once
#include "Dementia.h"

#if USING( ME_EDITOR )

#include "Math/Vector2.h"
#include <string>

class SceneViewWidget;

// The World view's sprite rect gizmo (ME-285): with one SpriteRenderer selected, its quad is
// outlined with handles on the edges and corners. Dragging one sets Size (the sprite resizes
// about its pivot), one undo step per drag. A 9-sliced sprite shows its borders as dashed guides.
namespace SpriteRectGizmo
{
    // Handles: 0 left, 1 right, 2 bottom, 3 top, 4..7 the corners (bottom-left, bottom-right,
    // top-right, top-left).
    enum Handle
    {
        Left = 0,
        Right,
        Bottom,
        Top,
        BottomLeft,
        BottomRight,
        TopRight,
        TopLeft,
        Count
    };

    // Called by the World view each frame; true while the gizmo owns the mouse (a handle hovered
    // or dragged).
    bool DrawViewport( SceneViewWidget& InView, const Vector2& InScreenMin, const Vector2& InScreenSize );
    // Where a handle of the selected sprite is on screen (ImGui's absolute coordinates); false
    // without one (edscript sprite-handle-mouse).
    bool GetHandleScreenPosition( SceneViewWidget& InView, int InHandle, Vector2& OutPosition );
    // The handle for a name ("right", "top-left", ...), or -1.
    int HandleFromName( const std::string& InName );
}

#endif
