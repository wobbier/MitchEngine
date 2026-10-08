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

    bool IsPointInFrustum( glm::vec4& inPoint );

    // Conservative: false only when the volume is entirely outside one of the planes.
    bool Intersects( const AABB& inBox ) const;
    bool Intersects( const Sphere& inSphere ) const;

private:
    bool IsOnPositiveSide( const glm::vec4& inPlane, const glm::vec3& inPoint );

    glm::vec4 Planes[FrustumPlane::Count];
};

