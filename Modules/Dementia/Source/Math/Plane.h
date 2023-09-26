#pragma once

#include "Vector3.h"

class Plane
{
public:
    Plane( const Vector3& InPoint1, const Vector3& InPoint2, const Vector3& InPoint3 );
    Plane() = default;

    void SetNormalAndPoint( const Vector3& InNormal, const Vector3& InPoint );

    float DistanceTo( const Vector3& InPoint );

private:
    Vector3 Normal;
    Vector3 Point;
    float Determinent;
};
