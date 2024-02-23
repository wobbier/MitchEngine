#pragma once

#include "Plane.h"
#include "Vector2.h"

#include "Matrix4.h"
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

private:
    bool IsOnPositiveSide( const glm::vec4& inPlane, const glm::vec3& inPoint );

    glm::vec4 Planes[FrustumPlane::Count];
};

