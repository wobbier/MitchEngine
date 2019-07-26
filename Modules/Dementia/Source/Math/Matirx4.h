#pragma once
#include <d3d11.h>
#include <DirectXMath.h>
#include "Vector3.h"

class Matrix4
{
public:
	Matrix4()
		: m_matrix(DirectX::XMMatrixIdentity())
	{

	}
	Matrix4(DirectX::SimpleMath::Matrix mat)
		: m_matrix(mat)
	{

	}


	DirectX::SimpleMath::Matrix& GetInternalMatrix()
	{
		return m_matrix;
	}

	Vector3 GetPosition()
	{
		return Vector3(m_matrix._41, m_matrix._42, m_matrix._43);
	}


private:
	DirectX::SimpleMath::Matrix m_matrix;
};