#pragma once
#include <d3d11.h>
#include <SimpleMath.h>
#include "LinearMath/btVector3.h"

class Vector3
{
public:

	static const Vector3 Up;

#pragma region Constructors

	Vector3(float x, float y, float z)
		: m_vector(x, y, z)
	{
	}
	Vector3(int x, int y, int z)
		: m_vector(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z))
	{
	}
	Vector3(float val)
		: m_vector(val)
	{
	}

	Vector3(const DirectX::XMVECTOR& other)
		: m_vector(std::move(other))
	{
	}

	// #TODO Make this private
	Vector3(const DirectX::SimpleMath::Vector3& other)
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

	Vector3 operator-(const Vector3& other) const
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
		return m_vector.x;
	}

	const float Y() const
	{
		return m_vector.y;
	}

	const float Z() const
	{
		return m_vector.z;
	}

	inline void SetX(float x)
	{
		m_vector.x = x;
	}

	inline void SetY(float y)
	{
		m_vector.y = y;
	}

	inline void SetZ(float z)
	{
		m_vector.z = z;
	}

	float& operator[](int index)
	{
		switch (index)
		{
		case 0:
			return m_vector.x;
		case 1:
			return m_vector.y;
		default:
			return m_vector.z;
		}
	}

	const float& operator[](int index) const
	{
		switch (index)
		{
		case 0:
			return m_vector.x;
		case 1:
			return m_vector.y;
		default:
			return m_vector.z;
		}
	}

	Vector3 Cross(const Vector3& other)
	{
		DirectX::SimpleMath::Vector3 vec = m_vector.Cross(other.GetInternalVec());
		
		return Vector3(vec);
	}

	inline const float Length() const
	{
		return m_vector.Length();
	}

	void Normalize()
	{
		DirectX::SimpleMath::Vector3 vec;
		m_vector.Normalize(vec);
		m_vector = vec;
	}

	Vector3 Normalized() const
	{
		DirectX::SimpleMath::Vector3 vec;
		m_vector.Normalize(vec);
		return Vector3(vec);
	}

	const DirectX::SimpleMath::Vector3& GetInternalVec() const
	{
		return m_vector;
	}
private:
	DirectX::SimpleMath::Vector3 m_vector;
};

__declspec(selectany) const Vector3 Vector3::Up = Vector3(0.f, 1.f, 0.f);

inline Vector3 operator*(float lhs, const Vector3 rhs)
{
	return Vector3(lhs * rhs.GetInternalVec());
}

inline Vector3 operator*(const Vector3 lhs, float rhs)
{
	return Vector3(lhs.GetInternalVec() * rhs);
}