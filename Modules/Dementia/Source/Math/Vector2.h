#pragma once
#include <glm/vec2.hpp>

struct Vector2
{
	union
	{
		struct {
			float x;
			float y;
		};
		glm::vec2 InternalVec;
	};

	Vector2()
		: InternalVec(0.f, 0.f)
	{
	}

	Vector2(const glm::vec2& other)
		: InternalVec(other)
	{
	}

	Vector2(float x, float y)
		: InternalVec(x, y)
	{
	}

	const bool IsZero()
	{
		static Vector2 Zero = Vector2(0.f, 0.f);
		return *this == Zero;
	}

	inline float Length() const
	{
		return std::sqrt(x * x + y * y);
	}

	float& operator[](int index)
	{
		return (&x)[index];
	}

	const float& operator[](int index) const
	{
		return (&x)[index];
	}

	Vector2 operator*(const Vector2& other)
	{
		return Vector2(InternalVec * other.InternalVec);
	}

	Vector2 operator*(const float& other)
	{
		return Vector2(InternalVec.x * other, InternalVec.y * other);
	}

	Vector2 operator+(const Vector2& other)
	{
		return Vector2(InternalVec + other.InternalVec);
	}

	Vector2 operator/(const float& other)
	{
		return Vector2(InternalVec.x / other, InternalVec.y / other);
	}

	bool operator==(const Vector2& other)
	{
		return InternalVec == other.InternalVec;
	}

	bool operator!=(const Vector2& other) const
	{
		return InternalVec != other.InternalVec;
	}

	bool operator<(const Vector2& other) const
	{
		return x < other.x && y < other.y;
	}

	bool operator>(const Vector2& other) const
	{
		return x > other.x && y > other.y;
	}
};

