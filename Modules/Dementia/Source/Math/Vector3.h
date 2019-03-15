#pragma once
#include <glm.hpp>
#include "LinearMath/btVector3.h"

class Vector3
{
public:

#pragma region Constructors

	Vector3(float x, float y, float z)
		: m_vector(x, y, z)
	{
	}
	Vector3(float val)
		: m_vector(val)
	{
	}

	// #TODO Make this private
	Vector3(const glm::vec3& other)
		: m_vector(std::move(other))
	{
	}

	Vector3()
		: m_vector(0.f, 0.f, 0.f)
	{
	}
#pragma endregion

#pragma region Operators
	Vector3 operator*(const Vector3& other)
	{
		return Vector3(m_vector * other.m_vector);
	}

	Vector3 operator+(const Vector3& other)
	{
		return Vector3(m_vector + other.m_vector);
	}

	Vector3 operator-(const Vector3& other)
	{
		return Vector3(m_vector - other.m_vector);
	}


	Vector3 operator-() const
	{
		return Vector3(-m_vector);
	}

	Vector3& operator+=(const Vector3& other)
	{
		this->m_vector += other.m_vector;
		return *this;
	}

	bool operator==(const Vector3& other)
	{
		return m_vector == other.m_vector;
	}

	Vector3 operator=(const Vector3& other)
	{
		return m_vector = other.m_vector;
	}

	bool operator!=(const Vector3& other)
	{
		return m_vector != other.m_vector;
	}

#pragma endregion

	const float X() const
	{
		return m_vector[0];
	}

	const float Y() const
	{
		return m_vector[1];
	}

	const float Z() const
	{
		return m_vector[2];
	}

	void SetX(float x)
	{
		m_vector[0] = x;
	}

	void SetY(float y)
	{
		m_vector[1] = y;
	}

	void SetZ(float z)
	{
		m_vector[2] = z;
	}

	float& operator[](int index)
	{
		return m_vector[index];
	}

	const float& operator[](int index) const
	{
		return m_vector[index];
	}

	Vector3 Cross(const Vector3& other)
	{
		return Vector3(glm::cross(m_vector, other.m_vector));
	}

	void Normalize()
	{
		m_vector = glm::normalize(m_vector);
	}

	Vector3 Normalized()
	{
		return Vector3(glm::normalize(m_vector));
	}

	const glm::vec3& GetInternalVec() const
	{
		return m_vector;
	}
private:
	glm::vec3 m_vector;
};

inline Vector3 operator*(float lhs, const Vector3 rhs)
{
	return Vector3(lhs * rhs.GetInternalVec());
}

inline Vector3 operator*(const Vector3 lhs, float rhs)
{
	return Vector3(lhs.GetInternalVec() * rhs);
}