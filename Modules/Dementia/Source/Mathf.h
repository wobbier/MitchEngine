#pragma once
#include "Math/Vector3.h"

namespace Mathf
{
	inline float Clamp(float Min, float Max, float Val)
	{
		if (Val < Min)
		{
			return Min;
		}
		if (Val > Max)
		{
			return Max;
		}
		return Val;
	}

	inline float Abs(float input)
	{
		if (input < 0.f)
		{
			input *= -1.f;
		}
		return input;
	}

	inline float Lerp(float v0, float v1, float t)
	{
		return (1.f - t) * v0 + t * v1;
	}

	inline Vector3 Lerp(Vector3& start, Vector3& end, float percent)
	{
		return (start + (end - start) * percent);
	}

	inline float Round(float number)
	{
		return std::round(number);
	}

	inline constexpr float Radians(float number)
	{
		return DirectX::XMConvertToRadians(number);
	}

	inline float Degrees(float rads)
	{
		return DirectX::XMConvertToDegrees(rads);
	}
}