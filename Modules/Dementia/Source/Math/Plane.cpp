#include "Plane.h"

Plane::Plane(const Vector3& InPoint1, const Vector3& InPoint2, const Vector3& InPoint3)
{
	Vector3 a = InPoint2 - InPoint1;
	Vector3 b = InPoint3 - InPoint1;

	Normal = a.Cross(b).Normalized();
	Determinent = -(Normal.x * InPoint1.x + Normal.y * InPoint1.y + Normal.z * InPoint1.z);

	Point = InPoint1;
}

void Plane::SetNormalAndPoint(const Vector3& InNormal, const Vector3& InPoint)
{
	Normal = InNormal.Normalized();
	Determinent = -(InNormal.Dot(InPoint));
}

float Plane::DistanceTo(const Vector3& InPoint)
{
	return (Determinent + Normal.Dot(InPoint));
}
