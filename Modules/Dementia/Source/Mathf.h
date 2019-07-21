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
		return (start + percent * (end - start));
	}

	inline float Round(float number)
	{
		return glm::round(number);
	}
}