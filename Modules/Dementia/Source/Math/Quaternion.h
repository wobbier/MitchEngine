#pragma once
#include <d3d11.h>
#include <SimpleMath.h>
#include "Vector3.h"
#define _USE_MATH_DEFINES
#include <math.h>

class Quaternion
{
public:
	Quaternion()
	{

	}
	Quaternion(const Vector3& Deg)
		: m_quat(DirectX::XMQuaternionRotationRollPitchYawFromVector(Deg.GetInternalVec()))
	{

	}
	Quaternion(const DirectX::SimpleMath::Quaternion& Quat)
		: m_quat(Quat)
	{

	}
	~Quaternion()
	{

	}

	float& operator[](int index)
	{
		switch (index)
		{
		case 0:
			return m_quat.x;
		case 1:
			return m_quat.y;
		case 2:
			return m_quat.z;
		default:
			return m_quat.w;
		}
	}

	const float& operator[](int index) const
	{
		switch (index)
		{
		case 0:
			return m_quat.x;
		case 1:
			return m_quat.y;
		case 2:
			return m_quat.z;
		default:
			return m_quat.w;
		}
	}

	Vector3 operator*(const Vector3& InPoint)
	{
		float num = m_quat.x * 2.f;
		float num2 = m_quat.y * 2.f;
		float num3 = m_quat.z * 2.f;
		float num4 = m_quat.x * num;
		float num5 = m_quat.y * num2;
		float num6 = m_quat.z * num3;
		float num7 = m_quat.x * num2;
		float num8 = m_quat.x * num3;
		float num9 = m_quat.y * num3;
		float num10 = m_quat.w * num;
		float num11 = m_quat.w * num2;
		float num12 = m_quat.w * num3;
		Vector3 result;
		result[0] = (1.f - (num5 + num6)) * InPoint[0] + (num7 - num12) * InPoint[1] + (num8 + num11) * InPoint[2];
		result[1] = (num7 + num12) * InPoint[0] + (1.f - (num4 + num6)) * InPoint[1] + (num9 - num10) * InPoint[2];
		result[2] = (num8 - num11) * InPoint[0] + (num9 + num10) * InPoint[1] + (1.f - (num4 + num5)) * InPoint[2];
		return result;
	}

	static Vector3 ToEulerAngles(Quaternion InQuat);

	static float Angle(Quaternion& a, Quaternion& b);

	static Quaternion AngleAxis(float InDegrees, const Vector3& InAxis);

	void Normalize();
	Quaternion Normalized();

	DirectX::SimpleMath::Quaternion& GetInternalVec()
	{
		return m_quat;
	}
private:
	DirectX::SimpleMath::Quaternion m_quat;
};