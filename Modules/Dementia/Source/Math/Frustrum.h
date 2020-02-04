#pragma once

#define _XM_NO_INTRINSICS_

#include "Plane.h"

#include <DirectXMath.h>

class Frustum
{
	enum FrustumPlane
	{
		Top = 0,
		Bottom,
		Left,
		Right,
		Near,
		Far
	};

public:
	Frustum();

	//void SetCameraInternals(float InAngle, float InRatio, float InNearDistance, float InFarDistance);

	//void SetCameraDef(const Vector3& InPosition, const Vector3& InLook, const Vector3& InUp);

	//bool PointInFrustum(const Vector3& InPoint);

	void TransformFrustum(const DirectX::XMFLOAT4X4& proj, const DirectX::XMFLOAT4X4& view);
	void Normalize(FrustumPlane plane);
	bool IsInside(const Vector3& point) const;

	Vector3 NearTopLeft;
	Vector3 NearTopRight;
	Vector3 NearBottomLeft;
	Vector3 NearBottomRight;
	Vector3 FarTopLeft;
	Vector3 FarTopRight;
	Vector3 FarBottomLeft;
	Vector3 FarBottomRight;
	float NearDistance;
	float FarDistance;
	float Ratio;
	float Angle;
	float Tang;
	float NearWidth;
	float NearHeight;
	float FarWidth;
	float FarHeight;
private:
	DirectX::SimpleMath::Vector4 Planes[6];
};

