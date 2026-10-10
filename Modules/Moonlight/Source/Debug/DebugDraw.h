#pragma once
// Immediate-mode debug drawing (lines) for gameplay code, cores and editor tools.
//
//   DebugDraw::Line( a, b, DebugDraw::Red );
//   DebugDraw::Box( worldBounds, DebugDraw::Yellow, 2.f );              // stays for 2 seconds
//   DebugDraw::Sphere( center, radius, DebugDraw::Green, 0.f, DebugDraw::NoDepthTest );
//
// Thread safe: shapes can be queued from jobs. Shapes with Duration 0 last one frame. Everything is
// rendered into every camera view (game cameras and the editor scene view) unless flagged
// EditorOnly, after the scene's opaque and transparent geometry.
#include "Math/Vector3.h"
#include "Math/Vector4.h"
#include "Math/Matrix4.h"
#include "Math/Bounds.h"
#include <cstdint>
#include <vector>

namespace DebugDraw
{
    enum Flags : uint8_t
    {
        None = 0,
        NoDepthTest = 1 << 0,   // Drawn on top of the scene.
        EditorOnly = 1 << 1,    // Only in the editor scene view (grids, selection, gizmos).
    };

    inline const Vector4 White( 1.f, 1.f, 1.f, 1.f );
    inline const Vector4 Black( 0.f, 0.f, 0.f, 1.f );
    inline const Vector4 Red( 1.f, 0.25f, 0.25f, 1.f );
    inline const Vector4 Green( 0.35f, 0.9f, 0.35f, 1.f );
    inline const Vector4 Blue( 0.3f, 0.5f, 1.f, 1.f );
    inline const Vector4 Yellow( 1.f, 0.86f, 0.1f, 1.f );
    inline const Vector4 Orange( 1.f, 0.6f, 0.05f, 1.f );
    inline const Vector4 Cyan( 0.15f, 0.75f, 1.f, 1.f );
    inline const Vector4 Magenta( 0.9f, 0.3f, 0.9f, 1.f );
    inline const Vector4 Gray( 0.5f, 0.5f, 0.5f, 1.f );

    void Line( const Vector3& InFrom, const Vector3& InTo, const Vector4& InColor = White, float InDuration = 0.f, uint8_t InFlags = None );
    void Ray( const Vector3& InOrigin, const Vector3& InDirection, const Vector4& InColor = White, float InDuration = 0.f, uint8_t InFlags = None );
    void Arrow( const Vector3& InFrom, const Vector3& InTo, const Vector4& InColor = White, float InHeadSize = 0.f, float InDuration = 0.f, uint8_t InFlags = None );

    void Box( const AABB& InBounds, const Vector4& InColor = White, float InDuration = 0.f, uint8_t InFlags = None );
    // Oriented box: InLocalBounds transformed by InTransform.
    void Box( const Matrix4& InTransform, const AABB& InLocalBounds, const Vector4& InColor = White, float InDuration = 0.f, uint8_t InFlags = None );

    void Circle( const Vector3& InCenter, const Vector3& InNormal, float InRadius, const Vector4& InColor = White, float InDuration = 0.f, uint8_t InFlags = None, int InSegments = 32 );
    void Sphere( const Vector3& InCenter, float InRadius, const Vector4& InColor = White, float InDuration = 0.f, uint8_t InFlags = None, int InSegments = 24 );
    void Capsule( const Vector3& InA, const Vector3& InB, float InRadius, const Vector4& InColor = White, float InDuration = 0.f, uint8_t InFlags = None );
    // Cone from InApex along InDirection, InLength long with half-angle InAngleDegrees.
    void Cone( const Vector3& InApex, const Vector3& InDirection, float InLength, float InAngleDegrees, const Vector4& InColor = White, float InDuration = 0.f, uint8_t InFlags = None );

    // Coordinate axes (X red, Y green, Z blue) of a transform.
    void Axes( const Matrix4& InTransform, float InSize = 1.f, float InDuration = 0.f, uint8_t InFlags = None );
    // The volume of a camera: pass the camera's view-projection matrix.
    void Frustum( const Matrix4& InViewProjection, const Vector4& InColor = White, float InDuration = 0.f, uint8_t InFlags = None );
    // Square grid on the plane spanned by InAxisA/InAxisB, InHalfCount cells each side of InCenter.
    void Grid( const Vector3& InCenter, const Vector3& InAxisA, const Vector3& InAxisB, int InHalfCount, float InSpacing, const Vector4& InColor = Gray, float InDuration = 0.f, uint8_t InFlags = None );

    // Filled, alpha-blended triangles (use a translucent colour). Always depth tested and never
    // written to depth, so they tint what they cover; NoDepthTest is ignored.
    void Triangle( const Vector3& InA, const Vector3& InB, const Vector3& InC, const Vector4& InColor, float InDuration = 0.f, uint8_t InFlags = None );
    // InCount positions, three per triangle.
    void Triangles( const Vector3* InPositions, size_t InCount, const Vector4& InColor, float InDuration = 0.f, uint8_t InFlags = None );

    // ---- Engine / renderer side -----------------------------------------------------------
    struct LineVertex
    {
        float X, Y, Z;
        uint32_t ABGR;
    };

    struct FrameLines
    {
        std::vector<LineVertex> Depth;          // depth tested, every view
        std::vector<LineVertex> Overlay;        // no depth test, every view
        std::vector<LineVertex> EditorDepth;    // depth tested, editor view only
        std::vector<LineVertex> EditorOverlay;  // no depth test, editor view only
        std::vector<LineVertex> Fill;           // triangles, every view
        std::vector<LineVertex> EditorFill;     // triangles, editor view only
    };

    // Copies the shapes to draw this frame (call once per frame before rendering).
    void CollectFrame( FrameLines& OutLines );
    // Ages timed shapes and drops one-frame shapes (call once per frame after rendering).
    void EndFrame( float InDeltaSeconds );
    void Clear();
    size_t GetLineCount();
}
