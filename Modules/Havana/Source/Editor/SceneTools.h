#pragma once
#include "Dementia.h"
#include "ECS/EntityHandle.h"
#include "Math/Bounds.h"
#include "Math/Matrix4.h"
#include "Math/Vector2.h"
#include <vector>

#if USING( ME_EDITOR )

class Entity;

// Scene-space helpers for the editor: bounds, ray picking and the scene view overlays drawn with
// DebugDraw (grid, selection boxes, camera/light gizmos).
namespace SceneTools
{
    // World bounds of the entity's meshes, including descendants when InRecursive.
    AABB ComputeWorldBounds( Entity& InEntity, bool InRecursive = true );
    // Union over the selection; entities without meshes contribute a small box at their position.
    AABB ComputeSelectionBounds();

    struct RayHit
    {
        EntityHandle Entity;
        float Distance = 0.f;
        Vector3 Point;
    };
    // Closest mesh hit (oriented bounds test, then nothing finer). False on a miss.
    bool Raycast( const Ray& InRay, RayHit& OutHit );

    // Builds a world ray through a viewport pixel (InPixel in [0, size)).
    Ray ScreenRay( const Vector2& InPixel, const Vector2& InViewportSize, const Matrix4& InView, const Matrix4& InProjection );
    // Projects a world point to viewport pixels. False when behind the camera.
    bool WorldToScreen( const Vector3& InPoint, const Vector2& InViewportSize, const Matrix4& InViewProjection, Vector2& OutPixel );

    struct OverlaySettings
    {
        bool ShowGrid = true;
        bool ShowSelection = true;
        bool ShowGizmos = true;
    };
    // Queues this frame's editor-only debug lines.
    void DrawOverlays( const Vector3& InCameraPosition, const OverlaySettings& InSettings );
}

#endif
