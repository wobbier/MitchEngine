#pragma once
#include <memory>

#include "Device/IDevice.h"
#include "Utils/StepTimer.h"
#include "Content/TestModelRenderer.h"

namespace Moonlight
{
	class Renderer
	{
	protected:
		Renderer();
		~Renderer() = default;

	public:
		static Renderer& Get()
		{
			static Renderer Instance;
			return Instance;
		}
		void SetWindow();
		void RegisterDeviceNotify(IDeviceNotify* deviceNotify);

		IDevice& GetDevice();

		void Update(float dt);
		void Render();

		void ReleaseDeviceDependentResources() const;
		void CreateDeviceDependentResources() const;
	private:
		IDevice* m_device;

#if ME_PLATFORM_UWP
		std::unique_ptr<TestModelRenderer> m_sceneRenderer;

		// Rendering loop timer.
		DX::StepTimer m_timer;
#endif
	};
}