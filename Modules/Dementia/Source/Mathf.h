#pragma once

namespace Mathf
{
	const float Clamp(float Min, float Max, float Val)
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
}