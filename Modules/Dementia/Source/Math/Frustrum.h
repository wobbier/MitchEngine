#pragma once

#include "Plane.h"
#include "Vector2.h"

#include "Matrix4.h"
#include "Bounds.h"
#include <glm/glm.hpp>

class Frustum
{
    enum FrustumPlane
    {
        Top = 0,
        Bottom,
        Left,
        Right,
        Near,
        Far,
        Count
    };

public:
    Frustum();

    void Update( Matrix4& inProjectionMatrix, Matrix4& inViewMatrix, float inFOV, Vector2& inOutputSize, float inNear, float inFar );
    // Planes straight from a view-projection matrix (shadow cascades, spot light views).
    void Update( const glm::mat4& inViewProjection );

    bool IsPointInFrustum( glm::vec4& inPoint );

    // Conservative: false only when the volume is entirely outside one of the planes.
    bool Intersects( const AABB& inBox ) const;
    bool Intersects( const Sphere& inSphere ) const;

private:
    bool IsOnPositiveSide( const glm::vec4& inPlane, const glm::vec3& inPoint );

    glm::vec4 Planes[FrustumPlane::Count];
};

