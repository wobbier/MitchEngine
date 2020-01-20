#include "Quaternion.h"

Vector3 Quaternion::ToEulerAngles(Quaternion InQuat)
{
	Vector3 angles;
	const DirectX::SimpleMath::Quaternion& q = InQuat.GetInternalVec();

	float sqw = q.w * q.w;
	float sqx = q.x * q.x;
	float sqy = q.y * q.y;
	float sqz = q.z * q.z;

	angles[1] = (float)std::atan2(2.f * q.x * q.w + 2.f * q.y * q.z, 1 - 2.f * (sqz + sqw));     // Yaw 
	angles[0] = (float)std::asin(2.f * (q.x * q.z - q.w * q.y));                             // Pitch 
	angles[2] = (float)std::atan2(2.f * q.x * q.y + 2.f * q.z * q.w, 1 - 2.f * (sqy + sqz));

	return angles;
}
