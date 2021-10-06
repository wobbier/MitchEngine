#include "Vector3.h"

const Vector3 Vector3::Right = Vector3(1.f, 0.f, 0.f);

const Vector3 Vector3::Front = Vector3(0.f, 0.f, 1.f);

const Vector3 Vector3::Up = Vector3(0.f, 1.f, 0.f);
//
//inline Vector3 operator* (const Vector3& v, float s)
//{
//	return Vector3(v.x * s, v.y * s, v.z * s);
//}
//
//inline Vector3 operator/ (const Vector3& v, float s)
//{
//	s = 1.f / s;
//	return Vector3(v.x * s, v.y * s, v.z * s);
//}
//
//inline Vector3 operator- (const Vector3& v)
//{
//	return Vector3(-v.x, -v.y, -v.z);
//}
//
//inline Vector3 operator+ (const Vector3& a, const Vector3& b)
//{
//	return Vector3(a.x + b.x, a.y + b.y, a.z + b.z);
//}
//
//inline Vector3 operator- (const Vector3& a, const Vector3& b)
//{
//	return Vector3(a.x - b.x, a.y - b.y, a.z - b.z);
//}
