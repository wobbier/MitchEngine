#pragma once
#include <memory>

#include "Device/IDevice.h"
#include "Utils/StepTimer.h"
#include "Singleton.h"
#include "Resource/ResourceCache.h"
#include "Graphics/FBXModel.h"
#include "Utils/DirectXHelper.h"

#if ME_ENABLE_RENDERDOC
#include "Debug/RenderDocManager.h"
#endif
#include <d3d11.h>

namespace Moonlight
{
	class Renderer
	{
	public:
		Renderer();
		virtual ~Renderer() final;

		void SetWindow();
		void RegisterDeviceNotify(IDeviceNotify* deviceNotify);

		IDevice& GetDevice();

		void Update(float dt);
		void Render();

		void ReleaseDeviceDependentResources();
		void CreateDeviceDependentResources();

		void PushModel(FBXModel* model);

		ResourceCache& GetResources();

		void WindowResized(const glm::vec2& NewSize);
	private:
		IDevice* m_device;

		ResourceCache Resources;

#if ME_DIRECTX
		Microsoft::WRL::ComPtr<ID3D11Buffer> m_constantBuffer;
		ModelViewProjectionConstantBuffer m_constantBufferData;
		float m_degreesPerSecond = 45.f;
#endif

		std::vector<FBXModel*> Models;
#if ME_ENABLE_RENDERDOC
		RenderDocManager* RenderDoc;
#endif

	};
}