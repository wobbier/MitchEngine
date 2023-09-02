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

	static const Vector3 Right;
	static const Vector3 Front;
	static const Vector3 Up;

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