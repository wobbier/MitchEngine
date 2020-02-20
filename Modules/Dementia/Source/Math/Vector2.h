#pragma once
#include <d3d11.h>
#include <SimpleMath.h>

class Vector2
{
public:
	Vector2()
		: m_vector(0.f, 0.f)
	{
	}

	Vector2(const DirectX::XMVECTOR& other)
		: m_vector(std::move(other))
	{
	}

	Vector2(DirectX::SimpleMath::Vector2& other)
		: m_vector(other)
	{
	}

	Vector2(DirectX::XMFLOAT2 vec)
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

	const bool IsZero()
	{
		static Vector2 Zero = Vector2(0.f, 0.f);
		return *this == Zero;
	}

	float& operator[](int index)
	{
		switch (index)
		{
		case 0:
			return m_vector.x;
		default:
			return m_vector.y;
		}
	}

	const float& operator[](int index) const
	{
		switch (index)
		{
		case 0:
			return m_vector.x;
		default:
			return m_vector.y;
		}
	}

	const DirectX::SimpleMath::Vector2& GetInternalVec() const
	{
		return m_vector;
	}

	Vector2 operator*(const Vector2& other)
	{
		return Vector2(m_vector * other.m_vector);
	}
	Vector2 operator+(const Vector2& other)
	{
		return Vector2(m_vector + other.m_vector);
	}
	Vector2 operator/(const float& other)
	{
		return Vector2(m_vector.x / other, m_vector.y / other);
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
	DirectX::SimpleMath::Vector2 m_vector;
};

