#pragma once
#include <glm.hpp>

class Vector3
{
public:
	Vector3(float x, float y, float z)
		: m_vector(x, y, z)
	{
	}

	Vector3(const glm::vec3& other)
		: m_vector(other)
	{
	}

	Vector3()
		: m_vector(0.f, 0.f, 0.f)
	{
	}

	Vector3 operator*(const Vector3& other)
	{
		return Vector3(m_vector * other.m_vector);
	}

	Vector3 operator*(const float& other)
	{
		return Vector3(m_vector * other);
	}

	Vector3 operator+(const Vector3& other)
	{
		return Vector3(m_vector + other.m_vector);
	}

	Vector3 operator-(const Vector3& other)
	{
		return Vector3(m_vector - other.m_vector);
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

	float operator[](float index)
	{
		return m_vector[index];
	}

	glm::vec3 GetInternalVec()
	{
		return m_vector;
	}
private:
	glm::vec3 m_vector;
};