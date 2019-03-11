#pragma once
#include "Math/Vector2.h"

namespace Moonlight
{
	struct IDeviceNotify
	{
		virtual void OnDeviceLost() = 0;
		virtual void OnDeviceRestored() = 0;
	};

	class IDevice
	{
	public:
		IDevice() = default;
		~IDevice() = default;

		virtual void CreateFactories() = 0;

		virtual void CreateDeviceResources() = 0;

		virtual void CreateWindowSizeDependentResources() = 0;

		virtual void Trim() = 0;

		virtual void SetLogicalSize(Vector2 vec2) = 0;

		virtual void WindowSizeChanged(const Vector2& NewSize) = 0;
	};
}