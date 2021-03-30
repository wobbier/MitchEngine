#pragma once

#define _USE_MATH_DEFINES
#include <math.h>

namespace DX
{
//	inline void ThrowIfFailed(HRESULT hr)
//	{
//		if (FAILED(hr))
//		{
//#if ME_PLATFORM_UWP
//			// Set a breakpoint on this line to catch Win32 API errors.
//			throw Platform::Exception::CreateException(hr);
//#elif ME_PLATFORM_WIN64
//			throw std::exception("The method or operation is not implemented.");
//#endif
//		}
//	}

	// Converts a length in device-independent pixels (DIPs) to a length in physical pixels.
	inline float ConvertDipsToPixels(float dips, float dpi)
	{
		static const float dipsPerInch = 96.0f;
		return floorf(dips * dpi / dipsPerInch + 0.5f); // Round to nearest integer.
	}

}