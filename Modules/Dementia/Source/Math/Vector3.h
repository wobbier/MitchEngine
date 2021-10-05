#pragma once
#include "LinearMath/btVector3.h"
#include <string>
#include <glm/glm.hpp>

struct Vector3
{
	union
	{
		struct {
			float x, y, z;
		};
		glm::vec3 InternalVector;
	};

	static const Vector3 Up;
	static const Vector3 Front;

	Vector3()
		: x(0.f)
		, y(0.f)
		, z(0.f)
	{
	}

	Vector3(float X, float Y, float Z)
		: x(X)
		, y(Y)
		, z(Z)
	{
	}

	Vector3(float V)
		: x(V)
		, y(V)
		, z(V)
	{
	}

	Vector3(const btVector3& v)
		: x(v.x())
		, y(v.y())
		, z(v.z())
	{
	}

	Vector3(const glm::vec3& v)
		: x(v.x)
		, y(v.y)
		, z(v.z)
	{
	}

	Vector3& operator=(const Vector3& other)
	{
		x = other.x;
		y = other.y;
		z = other.z;
		return *this;
	}

	float& operator[] (int i)
	{
		return (&x)[i];
	}

	const float& operator[] (int i) const
	{
		return (&x)[i];
	}

	bool operator== (const Vector3& v)
	{
		return x == v.x && y == v.y && z == v.z;
	}

	bool operator!= (const Vector3& v)
	{
		return x != v.x || y != v.y || z != v.z;
	}

	Vector3& operator*= (float s)
	{
		x *= s;
		y *= s;
		z *= s;
		return *this;
	}

	Vector3& operator/= (float s)
	{
		s = 1.f / s;
		x *= s;
		y *= s;
		z *= s;
		return *this;
	}

	Vector3& operator+= (const Vector3& v)
	{
		x += v.x;
		y += v.y;
		z += v.z;
		return *this;
	}

	Vector3& operator-= (const Vector3& v)
	{
		x -= v.x;
		y -= v.y;
		z -= v.z;
		return *this;
	}

	inline Vector3 operator* (float s) const
	{
		return Vector3(x * s, y * s, z * s);
	}

	inline Vector3 operator/ (float s) const
	{
		s = 1.f / s;
		return Vector3(x * s, y * s, z * s);
	}

	inline Vector3 operator- () const
	{
		return Vector3(-x, -y, -z);
	}

	inline Vector3 operator+ (const Vector3& b) const
	{
		return Vector3(x + b.x, y + b.y, z + b.z);
	}

	inline Vector3 operator- (const Vector3& b) const
	{
		return Vector3(x - b.x, y - b.y, z - b.z);
	}

	inline bool IsZero() const
	{
		return (x == 0.f && y == 0.f && z == 0.f);
	}

	inline float Length() const
	{
		return std::sqrt(x * x + y * y + z * z);
	}

	inline float LengthSquared() const
	{
		float s = (x * x + y * y + z * z);
		s *= s;
		return s;
	}

	inline Vector3& Normalize()
	{
		float m = Length();
		x /= m;
		y /= m;
		z /= m;
		return *this;
	}

	inline Vector3 Normalized() const
	{
		Vector3 ret(x, y, z);
		return Vector3(ret / Length());
	}

	inline float Dot(const Vector3& other) const
	{
		return (x * other.x + y * other.y + z * other.z);
	}

	inline Vector3 Cross(const Vector3& other) const
	{
		return Vector3(y * other.z - z * other.y, z * other.x - x * other.z, x * other.y - y * other.x);
	}
};

//class Vector3Dead
//{
//public:
//
//	static const Vector3 Up;
//	static const Vector3 Front;
//
//#pragma region Constructors
//
//	Vector3(float x, float y, float z)
//		: m_vector(x, y, z)
//	{
//	}
//	Vector3(int x, int y, int z)
//		: m_vector(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z))
//	{
//	}
//	Vector3(float val)
//		: m_vector(val)
//	{
//	}
//
//	Vector3(const DirectX::XMVECTOR& other)
//		: m_vector(std::move(other))
//	{
//	}
//
//	// #TODO Make this private
//	Vector3(const DirectX::SimpleMath::Vector3& other)
//		: m_vector(std::move(other))
//	{
//	}
//
//	Vector3()
//		: m_vector(0.f, 0.f, 0.f)
//	{
//	}
//#pragma endregion
//
//#pragma region Operators
//	Vector3 operator*(const Vector3& other) const
//	{
//		return Vector3(m_vector * other.m_vector);
//	}
//
//	Vector3 operator+(const Vector3& other)
//	{
//		return Vector3(m_vector + other.m_vector);
//	}
//
//	Vector3 operator-(const Vector3& other) const
//	{
//		return Vector3(m_vector - other.m_vector);
//	}
//
//	Vector3 operator/(const float& other) const
//	{
//		return Vector3(m_vector / other);
//	}
//
//	Vector3 operator-() const
//	{
//		return Vector3(-m_vector);
//	}
//
//	Vector3& operator+=(const Vector3& other)
//	{
//		this->m_vector += other.m_vector;
//		return *this;
//	}
//
//	bool operator==(const Vector3& other)
//	{
//		return m_vector == other.m_vector;
//	}
//
//	Vector3 operator=(const Vector3& other)
//	{
//		return m_vector = other.m_vector;
//	}
//
//	bool operator!=(const Vector3& other)
//	{
//		return m_vector != other.m_vector;
//	}
//
//#pragma endregion
//
//	const float X() const
//	{
//		return m_vector.x;
//	}
//
//	const float Y() const
//	{
//		return m_vector.y;
//	}
//
//	const float Z() const
//	{
//		return m_vector.z;
//	}
//
//	inline void SetX(float x)
//	{
//		m_vector.x = x;
//	}
//
//	inline void SetY(float y)
//	{
//		m_vector.y = y;
//	}
//
//	inline void SetZ(float z)
//	{
//		m_vector.z = z;
//	}
//
//	float& operator[](int index)
//	{
//		switch (index)
//		{
//		case 0:
//			return m_vector.x;
//		case 1:
//			return m_vector.y;
//		default:
//			return m_vector.z;
//		}
//	}
//
//	const float& operator[](int index) const
//	{
//		switch (index)
//		{
//		case 0:
//			return m_vector.x;
//		case 1:
//			return m_vector.y;
//		default:
//			return m_vector.z;
//		}
//	}
//
//	Vector3 Cross(const Vector3& other) const
//	{
//		DirectX::SimpleMath::Vector3 vec = m_vector.Cross(other.GetInternalVec());
//		
//		return Vector3(vec);
//	}
//
//	inline const float Length() const
//	{
//		return m_vector.Length();
//	}
//
//	void Normalize()
//	{
//		DirectX::SimpleMath::Vector3 vec;
//		m_vector.Normalize(vec);
//		m_vector = vec;
//	}
//
//	float Dot(const Vector3& Other) const
//	{
//		return m_vector.Dot(Other.GetInternalVec());
//	}
//
//	float LengthSquared() const 
//	{
//		return m_vector.LengthSquared();
//	}
//
//	Vector3 Normalized() const
//	{
//		DirectX::SimpleMath::Vector3 vec;
//		m_vector.Normalize(vec);
//		return Vector3(vec);
//	}
//
//	static Vector3 Lerp(const Vector3& Start, const Vector3& End, float Percent)
//	{
//		return Vector3(DirectX::SimpleMath::Vector3::Lerp(Start.GetInternalVec(), End.GetInternalVec(), Percent));
//	}
//
//	static Vector3 DistanceSq(const Vector3& InVec1, const Vector3& InVec2)
//	{
//		return Vector3(DirectX::SimpleMath::Vector3::DistanceSquared(InVec2.GetInternalVec(), InVec2.GetInternalVec()));
//	}
//
//	inline const DirectX::SimpleMath::Vector3& GetInternalVec() const
//	{
//		return m_vector;
//	}
//
//	std::string ToString()
//	{
//		return std::string("(" + std::to_string(m_vector.x) + ", " + std::to_string(m_vector.y) + ", " + std::to_string(m_vector.z) + ")");
//	}
//
//private:
//	DirectX::SimpleMath::Vector3 m_vector;
//};

//const Vector3 Vector3::Front = Vector3(0.f, 0.f, -1.f);

//const Vector3 Vector3::Up = Vector3(0.f, 1.f, 0.f);
//
//inline Vector3 operator* (const Vector3& v, float s)
//{
//	return Vector3(v.x * s, v.y * s, v.z * s);
//}
//
//inline Vector3 operator/ (const Vector3& v, float s)
//{
//	s = 1.f / s;
//	return Vector3(v.x * s, v.y * s, v.z * s);
//}
//
//inline Vector3 operator- (const Vector3& v)
//{
//	return Vector3(-v.x, -v.y, -v.z);
//}
//
//inline Vector3 operator+ (const Vector3& a, const Vector3& b)
//{
//	return Vector3(a.x + b.x, a.y + b.y, a.z + b.z);
//}
//
//inline Vector3 operator- (const Vector3& a, const Vector3& b)
//{
//	return Vector3(a.x - b.x, a.y - b.y, a.z - b.z);
//}
