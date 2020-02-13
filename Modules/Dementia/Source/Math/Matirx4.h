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

	const std::string ToString() const
	{
		std::string mat = std::to_string(m_matrix(0, 0)) + ",  " + std::to_string(m_matrix(0, 1)) + ", " + std::to_string(m_matrix(0, 2)) + ", " + std::to_string(m_matrix(0, 3)) + "\n";
		mat += std::to_string(m_matrix(1, 0)) + ", " + std::to_string(m_matrix(1, 1)) + ", " + std::to_string(m_matrix(1, 2)) + ", " + std::to_string(m_matrix(1, 3)) + "\n";
		mat += std::to_string(m_matrix(2, 0)) + ", " + std::to_string(m_matrix(2, 1)) + ", " + std::to_string(m_matrix(2, 2)) + ", " + std::to_string(m_matrix(2, 3)) + "\n";
		mat += std::to_string(m_matrix(3, 0)) + ", " + std::to_string(m_matrix(3, 1)) + ", " + std::to_string(m_matrix(3, 2)) + ", " + std::to_string(m_matrix(3, 3));
		return mat;
	}

	Vector3 operator*(const Matrix4& Mat)
	{
		return Vector3();
	}

private:
	DirectX::SimpleMath::Matrix m_matrix;
};