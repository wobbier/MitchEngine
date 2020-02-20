#pragma once
#include <math.h>

#include "Vector3.h"

class Line
{
public:
	Line() = default;
	Line(const Vector3& InStart, const Vector3& InEnd)
		: Start(InStart)
		, End(InEnd)
	{
	}

	Vector3 GetDirection() const
	{
		return End - Start;
	}

	Vector3 GetNormalizedDirection() const
	{
		return GetDirection().Normalized();
	}

	float Distance(const Line& Line1, const Line& Line2)
	{
		return std::sqrt(DistanceSq(Line1, Line2));
	}

	float DistanceSq(const Line& Line1, const Line& Line2)
	{
		Vector3 u = Line1.End - Line1.Start;
		Vector3 v = Line2.End - Line2.Start;
		Vector3 w = Line2.Start - Line1.Start;

		float a = u.Dot(u);
		float b = u.Dot(v);
		float c = v.Dot(v);
		float d = u.Dot(w);

		float determinent = a * c - b * b;

		if (determinent != 0.f)
		{
			float e = v.Dot(w);
			Vector3 U = (b * e - c * d) * u;
			Vector3 V = (a * e - b * d) * v;
			return (w + (U - V) / determinent).LengthSquared();
		}
		else
		{
			return 1.0f;// Vector3::DistanceSq(Line1.Start, Line2.Start + u * (d / b));
		}
	}

	Vector3 NearestPointTo(const Vector3& InPosition)
	{
		Vector3 u = End - Start;
		Vector3 v = InPosition - Start;

		return Start + (u.Dot(v) / u.LengthSquared()) * u;
	}

	float DistanceToPoint(const Vector3& InPoint)
	{
		return std::sqrt(DistanceSqToPoint(InPoint));
	}

	float DistanceSqToPoint(const Vector3& InPoint)
	{
		Vector3 pa = Start - InPoint;
		Vector3 n = End - Start;

		return (pa - (pa.Dot(n) / n.LengthSquared()) * n).LengthSquared();
	}

private:
	Vector3 Start;
	Vector3 End;
};