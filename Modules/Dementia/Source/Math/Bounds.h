#pragma once
#include "Vector3.h"
#include "Matrix4.h"
#include <algorithm>
#include <cfloat>
#include <cmath>

// Axis-aligned bounding box. Default constructed boxes are "empty" (min > max) and grow with Encapsulate.
struct AABB
{
    Vector3 Min = Vector3( FLT_MAX, FLT_MAX, FLT_MAX );
    Vector3 Max = Vector3( -FLT_MAX, -FLT_MAX, -FLT_MAX );

    AABB() = default;
    AABB( const Vector3& InMin, const Vector3& InMax ) : Min( InMin ), Max( InMax ) {}

    static AABB FromCenterExtents( const Vector3& InCenter, const Vector3& InExtents )
    {
        return AABB( InCenter - InExtents, InCenter + InExtents );
    }

    bool IsValid() const { return Min.x <= Max.x && Min.y <= Max.y && Min.z <= Max.z; }
    Vector3 GetCenter() const { return ( Min + Max ) * 0.5f; }
    Vector3 GetExtents() const { return ( Max - Min ) * 0.5f; }
    Vector3 GetSize() const { return Max - Min; }

    void Encapsulate( const Vector3& InPoint )
    {
        Min = Vector3( std::min( Min.x, InPoint.x ), std::min( Min.y, InPoint.y ), std::min( Min.z, InPoint.z ) );
        Max = Vector3( std::max( Max.x, InPoint.x ), std::max( Max.y, InPoint.y ), std::max( Max.z, InPoint.z ) );
    }

    void Encapsulate( const AABB& InOther )
    {
        if( InOther.IsValid() )
        {
            Encapsulate( InOther.Min );
            Encapsulate( InOther.Max );
        }
    }

    bool Contains( const Vector3& InPoint ) const
    {
        return InPoint.x >= Min.x && InPoint.x <= Max.x && InPoint.y >= Min.y && InPoint.y <= Max.y && InPoint.z >= Min.z && InPoint.z <= Max.z;
    }

    bool Intersects( const AABB& InOther ) const
    {
        return Min.x <= InOther.Max.x && Max.x >= InOther.Min.x
            && Min.y <= InOther.Max.y && Max.y >= InOther.Min.y
            && Min.z <= InOther.Max.z && Max.z >= InOther.Min.z;
    }

    // Box enclosing this box after an affine transform (Arvo's method).
    AABB Transformed( const Matrix4& InMatrix ) const
    {
        if( !IsValid() )
        {
            return *this;
        }
        const glm::mat4& m = InMatrix.GetInternalMatrix();
        const glm::vec3 center = glm::vec3( m * glm::vec4( GetCenter().InternalVector, 1.f ) );
        const glm::vec3 extents = GetExtents().InternalVector;
        glm::vec3 newExtents( 0.f );
        for( int axis = 0; axis < 3; ++axis )
        {
            newExtents[axis] = std::abs( m[0][axis] ) * extents.x + std::abs( m[1][axis] ) * extents.y + std::abs( m[2][axis] ) * extents.z;
        }
        return AABB( Vector3( center - newExtents ), Vector3( center + newExtents ) );
    }
};

struct Sphere
{
    Vector3 Center;
    float Radius = 0.f;

    Sphere() = default;
    Sphere( const Vector3& InCenter, float InRadius ) : Center( InCenter ), Radius( InRadius ) {}

    static Sphere FromAABB( const AABB& InBox )
    {
        return Sphere( InBox.GetCenter(), InBox.GetExtents().Length() );
    }

    bool Contains( const Vector3& InPoint ) const
    {
        return ( InPoint - Center ).LengthSquared() <= Radius * Radius;
    }

    bool Intersects( const Sphere& InOther ) const
    {
        const float radii = Radius + InOther.Radius;
        return ( InOther.Center - Center ).LengthSquared() <= radii * radii;
    }
};

struct Ray
{
    Vector3 Origin;
    // Unit length.
    Vector3 Direction = Vector3( 0.f, 0.f, 1.f );

    Ray() = default;
    Ray( const Vector3& InOrigin, const Vector3& InDirection ) : Origin( InOrigin ), Direction( InDirection.Normalized() ) {}

    Vector3 GetPoint( float InDistance ) const { return Origin + Direction * InDistance; }

    // Slab test. Returns the entry distance (0 when the origin is inside) or a negative value on a miss.
    float Intersect( const AABB& InBox, float InMaxDistance = FLT_MAX ) const
    {
        float tMin = 0.f;
        float tMax = InMaxDistance;
        for( int axis = 0; axis < 3; ++axis )
        {
            const float origin = Origin[axis];
            const float direction = Direction[axis];
            if( std::abs( direction ) < 1e-8f )
            {
                if( origin < InBox.Min[axis] || origin > InBox.Max[axis] )
                {
                    return -1.f;
                }
                continue;
            }
            const float inverse = 1.f / direction;
            float t0 = ( InBox.Min[axis] - origin ) * inverse;
            float t1 = ( InBox.Max[axis] - origin ) * inverse;
            if( t0 > t1 )
            {
                std::swap( t0, t1 );
            }
            tMin = std::max( tMin, t0 );
            tMax = std::min( tMax, t1 );
            if( tMin > tMax )
            {
                return -1.f;
            }
        }
        return tMin;
    }

    // Returns the nearest non-negative hit distance or a negative value on a miss.
    float Intersect( const Sphere& InSphere ) const
    {
        const Vector3 toCenter = Origin - InSphere.Center;
        const float b = toCenter.Dot( Direction );
        const float c = toCenter.LengthSquared() - InSphere.Radius * InSphere.Radius;
        if( c > 0.f && b > 0.f )
        {
            return -1.f;
        }
        const float discriminant = b * b - c;
        if( discriminant < 0.f )
        {
            return -1.f;
        }
        return std::max( 0.f, -b - std::sqrt( discriminant ) );
    }

    // Distance along the ray to a plane (normal, distance form: dot(n, p) + d = 0), negative on a miss.
    float IntersectPlane( const Vector3& InNormal, float InDistance ) const
    {
        const float denominator = InNormal.Dot( Direction );
        if( std::abs( denominator ) < 1e-8f )
        {
            return -1.f;
        }
        const float t = -( InNormal.Dot( Origin ) + InDistance ) / denominator;
        return t >= 0.f ? t : -1.f;
    }
};
