#include "Frustrum.h"
#include "Vector3.h"

#ifndef ANG2RAD
#define ANG2RAD 3.14159265358979323846/180.0
#endif

Frustum::Frustum()
{

}
//
//void Frustum::SetCameraInternals(float InAngle, float InRatio, float InNearDistance, float InFarDistance)
//{
//	Ratio = InRatio;
//	Angle = InAngle;
//	NearDistance = InNearDistance;
//	FarDistance = InFarDistance;
//
//	Tang = std::tan(ANG2RAD * Angle * .5f);
//	NearHeight = NearDistance * Tang;
//	NearWidth = NearHeight * Ratio;
//	FarHeight = FarDistance * Tang;
//	FarWidth = FarHeight * Ratio;
//}
//
//void Frustum::SetCameraDef(const Vector3& InPosition, const Vector3& InLook, const Vector3& InUp)
//{
//	Vector3 Direction;
//	Vector3 NearClip;
//	Vector3 FarClip;
//	Vector3 X;
//	Vector3 Y;
//	Vector3 Z;
//
//	Z = InPosition - InLook;
//	Z.Normalize();
//
//	X = InUp * Z;
//	X.Normalize();
//
//	Y = Z * X;
//
//	NearClip = InPosition - Z * NearDistance;
//	FarClip = InPosition - Z * FarDistance;
//
//	Planes[FrustumPlane::Near].SetNormalAndPoint(-Z, NearClip);
//	Planes[FrustumPlane::Far].SetNormalAndPoint(Z, FarClip);
//
//	Vector3 Aux;
//	Vector3 Normal;
//
//	{
//		Aux = (NearClip + Y * NearHeight);
//		Normal = (Aux - InPosition).Normalized() * X;
//		Planes[FrustumPlane::Top].SetNormalAndPoint(Normal, Aux);
//	}
//
//	{
//		Aux = (NearClip - Y * NearWidth);
//		Normal = X * (Aux - InPosition).Normalized();
//		Planes[FrustumPlane::Bottom].SetNormalAndPoint(Normal, Aux);
//	}
//
//	{
//		Aux = (NearClip - X * NearWidth);
//		Normal = (Aux - InPosition).Normalized() * Y;
//		Planes[FrustumPlane::Left].SetNormalAndPoint(Normal, Aux);
//	}
//
//	{
//		Aux = (NearClip + X * NearWidth);
//		Normal = Y * (Aux - InPosition).Normalized();
//		Planes[FrustumPlane::Right].SetNormalAndPoint(Normal, Aux);
//	}
//}
//
//bool Frustum::PointInFrustum(const Vector3& InPoint)
//{
//	for (int i = 0; i < 6; ++i)
//	{
//		float Distance = Planes[i].DistanceTo(InPoint);
//		if (Distance < 0.f)
//		{
//			return false;
//		}
//	}
//
//	return true;
//}
//
//void Frustum::TransformFrustum(const DirectX::XMFLOAT4X4& proj, const DirectX::XMFLOAT4X4& view)
//{
//	float clip[4][4];
//	clip[0][0] = (view(0, 0) * proj(0, 0) + view(0, 1) * proj(1, 0) + view(0, 2) * proj(2, 0) + view(0, 3) * proj(3, 0));
//	clip[0][1] = view(0, 0) * proj(0, 1) + view(0, 1) * proj(1, 1) + view(0, 2) * proj(2, 1) + view(0, 3) * proj(3, 1);
//	clip[0][2] = view(0, 0) * proj(0, 2) + view(0, 1) * proj(1, 2) + view(0, 2) * proj(2, 2) + view(0, 3) * proj(3, 2);
//	clip[0][3] = view(0, 0) * proj(0, 3) + view(0, 1) * proj(1, 3) + view(0, 2) * proj(2, 3) + view(0, 3) * proj(3, 3);
//
//	clip[1][0] = view(1, 0) * proj(0, 0) + view(1, 1) * proj(1, 0) + view(1, 2) * proj(2, 0) + view(1, 3) * proj(3, 0);
//	clip[1][1] = view(1, 0) * proj(0, 1) + view(1, 1) * proj(1, 1) + view(1, 2) * proj(2, 1) + view(1, 3) * proj(3, 1);
//	clip[1][2] = view(1, 0) * proj(0, 2) + view(1, 1) * proj(1, 2) + view(1, 2) * proj(2, 2) + view(1, 3) * proj(3, 2);
//	clip[1][3] = view(1, 0) * proj(0, 3) + view(1, 1) * proj(1, 3) + view(1, 2) * proj(2, 3) + view(1, 3) * proj(3, 3);
//
//	clip[2][0] = view(2, 0) * proj(0, 0) + view(2, 1) * proj(1, 0) + view(2, 2) * proj(2, 0) + view(2, 3) * proj(3, 0);
//	clip[2][1] = view(2, 0) * proj(0, 1) + view(2, 1) * proj(1, 1) + view(2, 2) * proj(2, 1) + view(2, 3) * proj(3, 1);
//	clip[2][2] = view(2, 0) * proj(0, 2) + view(2, 1) * proj(1, 2) + view(2, 2) * proj(2, 2) + view(2, 3) * proj(3, 2);
//	clip[2][3] = view(2, 0) * proj(0, 3) + view(2, 1) * proj(1, 3) + view(2, 2) * proj(2, 3) + view(2, 3) * proj(3, 3);
//
//	clip[3][0] = view(3, 0) * proj(0, 0) + view(3, 1) * proj(1, 0) + view(3, 2) * proj(2, 0) + view(3, 3) * proj(3, 0);
//	clip[3][1] = view(3, 0) * proj(0, 1) + view(3, 1) * proj(1, 1) + view(3, 2) * proj(2, 1) + view(3, 3) * proj(3, 1);
//	clip[3][2] = view(3, 0) * proj(0, 2) + view(3, 1) * proj(1, 2) + view(3, 2) * proj(2, 2) + view(3, 3) * proj(3, 2);
//	clip[3][3] = view(3, 0) * proj(0, 3) + view(3, 1) * proj(1, 3) + view(3, 2) * proj(2, 3) + view(3, 3) * proj(3, 3);
//
//	Planes[FrustumPlane::Right].x = clip[0][3] - clip[0][0];
//	Planes[FrustumPlane::Right].y = clip[1][3] - clip[1][0];
//	Planes[FrustumPlane::Right].z = clip[2][3] - clip[2][0];
//	Planes[FrustumPlane::Right].w = clip[3][3] - clip[3][0];
//	Normalize(Right);
//
//	Planes[FrustumPlane::Left].x = clip[0][3] + clip[0][0];
//	Planes[FrustumPlane::Left].y = clip[1][3] + clip[1][0];
//	Planes[FrustumPlane::Left].z = clip[2][3] + clip[2][0];
//	Planes[FrustumPlane::Left].w = clip[3][3] + clip[3][0];
//	Normalize(Left);
//
//	Planes[FrustumPlane::Bottom].x = clip[0][3] + clip[0][1];
//	Planes[FrustumPlane::Bottom].y = clip[1][3] + clip[1][1];
//	Planes[FrustumPlane::Bottom].z = clip[2][3] + clip[2][1];
//	Planes[FrustumPlane::Bottom].w = clip[3][3] + clip[3][1];
//	Normalize(Bottom);
//
//	Planes[Top].x = clip[0][3] - clip[0][1];
//	Planes[Top].y = clip[1][3] - clip[1][1];
//	Planes[Top].z = clip[2][3] - clip[2][1];
//	Planes[Top].w = clip[3][3] - clip[3][1];
//	Normalize(Top);
//
//	Planes[Far].x = clip[0][3] - clip[0][2];
//	Planes[Far].y = clip[1][3] - clip[1][2];
//	Planes[Far].z = clip[2][3] - clip[2][2];
//	Planes[Far].w = clip[3][3] - clip[3][2];
//	Normalize(Far);
//
//	Planes[Near].x = clip[0][3] + clip[0][2];
//	Planes[Near].y = clip[1][3] + clip[1][2];
//	Planes[Near].z = clip[2][3] + clip[2][2];
//	Planes[Near].w = clip[3][3] + clip[3][2];
//	Normalize(Near);
//}
//
//void Frustum::Normalize(FrustumPlane plane)
//{
//	double magnitude = std::sqrt(
//		Planes[plane].x * Planes[plane].x +
//		Planes[plane].y * Planes[plane].y +
//		Planes[plane].z * Planes[plane].z
//	);
//
//	Planes[plane].x /= magnitude;
//	Planes[plane].y /= magnitude;
//	Planes[plane].z /= magnitude;
//	Planes[plane].w /= magnitude;
//}
//
//bool Frustum::IsInside(const Vector3& point) const
//{
//	for (unsigned int i = 0; i < 6; i++)
//	{
//		if (Planes[i].x * point[0] +
//			Planes[i].y * point[1] +
//			Planes[i].z * point[2] +
//			Planes[i].w <= 0) {
//			return false;
//		}
//	}
//
//	return true;
//}
