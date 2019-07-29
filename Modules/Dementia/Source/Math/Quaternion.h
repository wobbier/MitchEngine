#pragma once
#include <d3d11.h>
#include <SimpleMath.h>
#include "Vector3.h"

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
	~Quaternion()
	{

	}

	DirectX::SimpleMath::Vector4& GetInternalVec()
	{
		return m_vector;
	}
private:
	DirectX::SimpleMath::Vector4 m_vector;
};