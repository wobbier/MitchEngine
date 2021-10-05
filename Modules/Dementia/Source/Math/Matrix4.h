#pragma once
#include "Vector3.h"

#include <glm/gtc/matrix_transform.hpp>
#include "Quaternion.h"

class Matrix4
{
public:
	Matrix4()
		: m_matrix(1.f)
	{

	}
	Matrix4(const glm::mat4& mat)
		: m_matrix(mat)
	{

	}

	glm::mat4& GetInternalMatrix()
	{
		return m_matrix;
	}

	Vector3 GetPosition()
	{
		return Vector3(m_matrix[3][0]/*_41*/, m_matrix[3][1]/*._42*/, m_matrix[3][2]/*._43*/);
	}

	Quaternion GetRotation()
	{
		return Quaternion(glm::quat_cast(GetInternalMatrix()));
	}

	Matrix4 Inverse()
	{
		return glm::inverse(GetInternalMatrix());
	}

	const std::string ToString() const
	{
		std::string mat = std::to_string(m_matrix[0][0]) + ", " + std::to_string(m_matrix[0][1]) + ", " + std::to_string(m_matrix[0][2]) + ", " + std::to_string(m_matrix[0][3]) + "\n";
		mat += std::to_string(m_matrix[1][0]) + ", " + std::to_string(m_matrix[1][1]) + ", " + std::to_string(m_matrix[1][2]) + ", " + std::to_string(m_matrix[1][3]) + "\n";
		mat += std::to_string(m_matrix[2][0]) + ", " + std::to_string(m_matrix[2][1]) + ", " + std::to_string(m_matrix[2][2]) + ", " + std::to_string(m_matrix[2][3]) + "\n";
		mat += std::to_string(m_matrix[3][0]) + ", " + std::to_string(m_matrix[3][1]) + ", " + std::to_string(m_matrix[3][2]) + ", " + std::to_string(m_matrix[3][3]);
		return mat;
	}

	Vector3 operator*(const Matrix4& Mat)
	{
		return Vector3();
	}

private:
	glm::mat4 m_matrix;
};