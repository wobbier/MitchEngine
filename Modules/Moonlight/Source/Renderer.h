#pragma once
#include <memory>

#include "Device/IDevice.h"
#include "Utils/StepTimer.h"
#include "Content/TestModelRenderer.h"
#include "Singleton.h"
#include "Resource/ResourceCache.h"
#include "Graphics/FBXModel.h"

namespace Moonlight
{
	class Renderer
		: public Singleton<Renderer>
	{
	public:
		Renderer();
		virtual ~Renderer() final;

		void SetWindow();
		void RegisterDeviceNotify(IDeviceNotify* deviceNotify);

		IDevice& GetDevice();

		void Update(float dt);
		void Render();

		void ReleaseDeviceDependentResources() const;
		void CreateDeviceDependentResources() const;

		void PushModel(FBXModel* model);

		ResourceCache& GetResources();
	private:
		IDevice* m_device;

		ResourceCache Resources;

#if ME_PLATFORM_UWP
		std::unique_ptr<TestModelRenderer> m_sceneRenderer;

		// Rendering loop timer.
		DX::StepTimer m_timer;
#endif
		std::vector<FBXModel*> Models;
	};
}