#include "Quaternion.h"
#include <algorithm>
#include "Mathf.h"

Vector3 Quaternion::ToEulerAngles(Quaternion InQuat)
{
	Vector3 angles;
	const DirectX::SimpleMath::Quaternion& q = InQuat.GetInternalVec();

	// roll (x-axis rotation)
	double sinr_cosp = 2 * (q.w * q.x + q.y * q.z);
	double cosr_cosp = 1 - 2 * (q.x * q.x + q.y * q.y);
	angles.SetX(std::atan2(sinr_cosp, cosr_cosp));

	// pitch (y-axis rotation)
	double sinp = 2 * (q.w * q.y - q.z * q.x);
	if (std::abs(sinp) >= 1)
		angles.SetY(std::copysign(M_PI / 2, sinp)); // use 90 degrees if out of range
	else
		angles.SetY(std::asin(sinp));

	// yaw (z-axis rotation)
	double siny_cosp = 2 * (q.w * q.z + q.x * q.y);
	double cosy_cosp = 1 - 2 * (q.y * q.y + q.z * q.z);
	angles.SetZ(std::atan2(siny_cosp, cosy_cosp));

	return angles;
}

float Quaternion::Angle(Quaternion& a, Quaternion& b)
{
	float f = a.GetInternalVec().Dot(b.GetInternalVec());

	return std::acos(std::min(std::abs(f), 1.f)) * 2.f * 57.29578f;
}

Quaternion Quaternion::AngleAxis(float InDegrees, const Vector3& InAxis)
{
	//if (InAxis.LengthSquared() == 0.0f)
	//	return Quaternion(DirectX::SimpleMath::Quaternion::Identity);

	//Quaternion result = DirectX::SimpleMath::Quaternion::Identity;
	//float radians = Mathf::Radians(InDegrees);
	//radians *= 0.5f;

	//Vector3 normalizedAxis = InAxis.Normalized();
	//normalizedAxis = normalizedAxis * (float)std::sin(radians);
	//result[0] = normalizedAxis.X();
	//result[1] = normalizedAxis.Y();
	//result[2] = normalizedAxis.Z();
	//result[3] = (float)std::cos(radians);

	//return result.Normalized();
	return Quaternion(DirectX::SimpleMath::Quaternion::CreateFromAxisAngle(InAxis.GetInternalVec(), Mathf::Radians(InDegrees)));
}

void Quaternion::Normalize()
{
	m_quat.Normalize(m_quat);
}

Quaternion Quaternion::Normalized()
{
	DirectX::SimpleMath::Quaternion tempQuat;
	m_quat.Normalize(tempQuat);
	return Quaternion(tempQuat);
}
