#pragma once
#include <glm.hpp>

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

		virtual void Present()
		{
		}

		virtual void CreateDeviceIndependentResources()
		{
		}

		virtual void CreateDeviceResources()
		{
		}

		virtual void CreateWindowSizeDependentResources()
		{
		}

		virtual void Trim()
		{
		}

		virtual void SetLogicalSize(glm::vec2 vec2)
		{
		}
	};
}