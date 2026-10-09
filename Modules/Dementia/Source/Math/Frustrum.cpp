#include "Frustrum.h"
#include "Vector3.h"
#include "optick.h"

#ifndef ANG2RAD
#define ANG2RAD 3.14159265358979323846/180.0
#endif

Frustum::Frustum()
{
    memset( &Planes[0], 0, sizeof( glm::vec4 ) * FrustumPlane::Count );
}

void Frustum::Update( Matrix4& inProjectionMatrix, Matrix4& inViewMatrix, float inFOV, Vector2& inOutputSize, float inNear, float inFar )
{
    if( inOutputSize.IsZero() )
    {
        return;
    }

    Update( inProjectionMatrix.GetInternalMatrix() * inViewMatrix.GetInternalMatrix() );
}


void Frustum::Update( const glm::mat4& vpMatrix )
{
    // Left plane
    Planes[FrustumPlane::Left] = glm::vec4(
        vpMatrix[0][3] + vpMatrix[0][0],
        vpMatrix[1][3] + vpMatrix[1][0],
        vpMatrix[2][3] + vpMatrix[2][0],
        vpMatrix[3][3] + vpMatrix[3][0] );

    // Right plane
    Planes[FrustumPlane::Right] = glm::vec4(
        vpMatrix[0][3] - vpMatrix[0][0],
        vpMatrix[1][3] - vpMatrix[1][0],
        vpMatrix[2][3] - vpMatrix[2][0],
        vpMatrix[3][3] - vpMatrix[3][0] );

    // Bottom plane
    Planes[FrustumPlane::Bottom] = glm::vec4(
        vpMatrix[0][3] + vpMatrix[0][1],
        vpMatrix[1][3] + vpMatrix[1][1],
        vpMatrix[2][3] + vpMatrix[2][1],
        vpMatrix[3][3] + vpMatrix[3][1] );

    // Top plane
    Planes[FrustumPlane::Top] = glm::vec4(
        vpMatrix[0][3] - vpMatrix[0][1],
        vpMatrix[1][3] - vpMatrix[1][1],
        vpMatrix[2][3] - vpMatrix[2][1],
        vpMatrix[3][3] - vpMatrix[3][1] );

    // Near plane
    Planes[FrustumPlane::Near] = glm::vec4(
        vpMatrix[0][3] + vpMatrix[0][2],
        vpMatrix[1][3] + vpMatrix[1][2],
        vpMatrix[2][3] + vpMatrix[2][2],
        vpMatrix[3][3] + vpMatrix[3][2] );

    // Far plane
    Planes[FrustumPlane::Far] = glm::vec4(
        vpMatrix[0][3] - vpMatrix[0][2],
        vpMatrix[1][3] - vpMatrix[1][2],
        vpMatrix[2][3] - vpMatrix[2][2],
        vpMatrix[3][3] - vpMatrix[3][2] );

    // Normalize the planes
    for( auto& plane : Planes ) {
        float length = glm::length( glm::vec3( plane.x, plane.y, plane.z ) );
        plane /= length;
    }
}

bool Frustum::IsPointInFrustum( glm::vec4& inPoint )
{
    //OPTICK_CATEGORY( "Culling", Optick::Category::Visibility );
    for( const auto& plane : Planes )
    {
        if( !IsOnPositiveSide( plane, inPoint ) )
        {
            return false;
        }
    }

    return true;
}

bool Frustum::Intersects( const AABB& inBox ) const
{
    if( !inBox.IsValid() )
    {
        return false;
    }
    for( const glm::vec4& plane : Planes )
    {
        // The box corner furthest along the plane normal; if even that is behind, the box is out.
        const glm::vec3 positive(
            plane.x >= 0.f ? inBox.Max.x : inBox.Min.x,
            plane.y >= 0.f ? inBox.Max.y : inBox.Min.y,
            plane.z >= 0.f ? inBox.Max.z : inBox.Min.z );
        if( glm::dot( glm::vec3( plane ), positive ) + plane.w < 0.f )
        {
            return false;
        }
    }
    return true;
}

bool Frustum::Intersects( const Sphere& inSphere ) const
{
    for( const glm::vec4& plane : Planes )
    {
        if( glm::dot( glm::vec3( plane ), inSphere.Center.InternalVector ) + plane.w < -inSphere.Radius )
        {
            return false;
        }
    }
    return true;
}

bool Frustum::IsOnPositiveSide( const glm::vec4& inPlane, const glm::vec3& inPoint )
{
    return glm::dot( glm::vec3( inPlane ), inPoint ) + inPlane.w > 0;
}