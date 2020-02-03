#include "Frustrum.h"
#include "Vector3.h"

#ifndef _XM_NO_INTRINSICS_
#define _XM_NO_INTRINSICS_
#endif

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

void Frustum::TransformFrustum(const DirectX::XMMATRIX& proj, const DirectX::XMMATRIX& view)
{
	double clip[4][4];
	clip[0][0] = (view(0, 0) * proj(0, 0) + view(0, 1) * proj(1, 0) + view(0, 2) * proj(2, 0) + view(0, 3) * proj(3, 0));
	clip[0][1] = view(0, 0) * proj(0, 1) + view(0, 1) * proj(1, 1) + view(0, 2) * proj(2, 1) + view(0, 3) * proj(3, 1);
	clip[0][2] = view(0, 0) * proj(0, 2) + view(0, 1) * proj(1, 2) + view(0, 2) * proj(2, 2) + view(0, 3) * proj(3, 2);
	clip[0][3] = view(0, 0) * proj(0, 3) + view(0, 1) * proj(1, 3) + view(0, 2) * proj(2, 3) + view(0, 3) * proj(3, 3);

	clip[1][0] = view(1, 0) * proj(0, 0) + view(1, 1) * proj(1, 0) + view(1, 2) * proj(2, 0) + view(1, 3) * proj(3, 0);
	clip[1][1] = view(1, 0) * proj(0, 1) + view(1, 1) * proj(1, 1) + view(1, 2) * proj(2, 1) + view(1, 3) * proj(3, 1);
	clip[1][2] = view(1, 0) * proj(0, 2) + view(1, 1) * proj(1, 2) + view(1, 2) * proj(2, 2) + view(1, 3) * proj(3, 2);
	clip[1][3] = view(1, 0) * proj(0, 3) + view(1, 1) * proj(1, 3) + view(1, 2) * proj(2, 3) + view(1, 3) * proj(3, 3);

	clip[2][0] = view(2, 0) * proj(0, 0) + view(2, 1) * proj(1, 0) + view(2, 2) * proj(2, 0) + view(2, 3) * proj(3, 0);
	clip[2][1] = view(2, 0) * proj(0, 1) + view(2, 1) * proj(1, 1) + view(2, 2) * proj(2, 1) + view(2, 3) * proj(3, 1);
	clip[2][2] = view(2, 0) * proj(0, 2) + view(2, 1) * proj(1, 2) + view(2, 2) * proj(2, 2) + view(2, 3) * proj(3, 2);
	clip[2][3] = view(2, 0) * proj(0, 3) + view(2, 1) * proj(1, 3) + view(2, 2) * proj(2, 3) + view(2, 3) * proj(3, 3);

	clip[3][0] = view(3, 0) * proj(0, 0) + view(3, 1) * proj(1, 0) + view(3, 2) * proj(2, 0) + view(3, 3) * proj(3, 0);
	clip[3][1] = view(3, 0) * proj(0, 1) + view(3, 1) * proj(1, 1) + view(3, 2) * proj(2, 1) + view(3, 3) * proj(3, 1);
	clip[3][2] = view(3, 0) * proj(0, 2) + view(3, 1) * proj(1, 2) + view(3, 2) * proj(2, 2) + view(3, 3) * proj(3, 2);
	clip[3][3] = view(3, 0) * proj(0, 3) + view(3, 1) * proj(1, 3) + view(3, 2) * proj(2, 3) + view(3, 3) * proj(3, 3);

	m_data[Right][0] = clip[0][3] - clip[0][0];
	m_data[Right][1] = clip[1][3] - clip[1][0];
	m_data[Right][2] = clip[2][3] - clip[2][0];
	m_data[Right][3] = clip[3][3] - clip[3][0];
	Normalize(Right);

	m_data[Left][0] = clip[0][3] + clip[0][0];
	m_data[Left][1] = clip[1][3] + clip[1][0];
	m_data[Left][2] = clip[2][3] + clip[2][0];
	m_data[Left][3] = clip[3][3] + clip[3][0];
	Normalize(Left);

	m_data[Bottom][0] = clip[0][3] + clip[0][1];
	m_data[Bottom][1] = clip[1][3] + clip[1][1];
	m_data[Bottom][2] = clip[2][3] + clip[2][1];
	m_data[Bottom][3] = clip[3][3] + clip[3][1];
	Normalize(Bottom);

	m_data[Top][0] = clip[0][3] - clip[0][1];
	m_data[Top][1] = clip[1][3] - clip[1][1];
	m_data[Top][2] = clip[2][3] - clip[2][1];
	m_data[Top][3] = clip[3][3] - clip[3][1];
	Normalize(Top);

	m_data[Front][0] = clip[0][3] - clip[0][2];
	m_data[Front][1] = clip[1][3] - clip[1][2];
	m_data[Front][2] = clip[2][3] - clip[2][2];
	m_data[Front][3] = clip[3][3] - clip[3][2];
	Normalize(Front);

	m_data[Back][0] = clip[0][3] + clip[0][2];
	m_data[Back][1] = clip[1][3] + clip[1][2];
	m_data[Back][2] = clip[2][3] + clip[2][2];
	m_data[Back][3] = clip[3][3] + clip[3][2];
	Normalize(Back);
}

void Frustum::Normalize(FrustumPlane plane)
{
	double magnitude = std::sqrt(
		m_data[plane][0] * m_data[plane][0] +
		m_data[plane][1] * m_data[plane][1] +
		m_data[plane][2] * m_data[plane][2]
	);

	m_data[plane][0] /= magnitude;
	m_data[plane][1] /= magnitude;
	m_data[plane][2] /= magnitude;
	m_data[plane][3] /= magnitude;
}

bool Frustum::IsInside(const Vector3& point) const
{
	for (unsigned int i = 0; i < 6; i++)
	{
		if (m_data[i][0] * point.x +
			m_data[i][1] * point.y +
			m_data[i][2] * point.z +
			m_data[i][3] <= 0) {
			return false;
		}
	}

	return true;
}
