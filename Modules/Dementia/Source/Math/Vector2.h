#pragma once
#include <glm.hpp>

class Vector2
{
public:
	Vector2()
		: m_vector(0.f, 0.f)
	{
	}

	Vector2(glm::vec2 vec)
		: m_vector(vec)
	{
	}

	Vector2(float x, float y)
		: m_vector(x, y)
	{
	}

	void SetX(float x)
	{
		m_vector.x = x;
	}

	const float X() const
	{
		return m_vector.x;
	}

	void SetY(float y)
	{
		m_vector.y = y;
	}

	const float Y() const
	{
		return m_vector.y;
	}

	Vector2 operator*(const Vector2& other)
	{
		return Vector2(m_vector * other.m_vector);
	}
	Vector2 operator+(const Vector2& other)
	{
		return Vector2(m_vector + other.m_vector);
	}
	bool operator==(const Vector2& other)
	{
		return m_vector == other.m_vector;
	}
	bool operator!=(const Vector2& other)
	{
		return m_vector != other.m_vector;
	}

private:
	glm::vec2 m_vector;
};