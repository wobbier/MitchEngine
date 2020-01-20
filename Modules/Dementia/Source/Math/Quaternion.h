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
		: m_vector(DirectX::XMQuaternionRotationRollPitchYawFromVector(Deg.GetInternalVec()))
	{

	}
	Quaternion(const DirectX::SimpleMath::Quaternion& Quat)
		: m_vector(Quat)
	{

	}
	~Quaternion()
	{

	}

	static Vector3 ToEulerAngles(Quaternion InQuat);

	DirectX::SimpleMath::Quaternion& GetInternalVec()
	{
		return m_vector;
	}
private:
	DirectX::SimpleMath::Quaternion m_vector;
};