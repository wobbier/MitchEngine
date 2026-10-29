#pragma once
#include "Dementia.h"

#if USING( ME_EDITOR )

#include "Math/Vector2.h"
#include <string>

class SceneViewWidget;
class Entity;

// The World view's tile brush (LOOK-S5): with Tilemap's Paint on and a tilemap selected, the
// left button paints Tilemap::s_paintIndex into the cell under the mouse (on the tilemap's plane)
// and the right button erases. One undo step per stroke.
namespace TilemapPainter
{
    // Called by the World view each frame; true when the brush owns the mouse.
    bool DrawViewport( SceneViewWidget& InView, const Vector2& InScreenMin, const Vector2& InScreenSize );
    // Paints (or with -1 erases) the cell under a point of the view (pixels of its render size), as
    // the brush would, recorded as one undo step. False when the point misses the tilemap's plane.
    bool PaintAt( SceneViewWidget& InView, Entity& InEntity, const Vector2& InViewportPixel, int InPaletteIndex );
}

#endif
