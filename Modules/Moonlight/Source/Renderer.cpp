#include "Renderer.h"
#include "Device/D3D12Device.h"
#include "Device/GLDevice.h"

namespace Moonlight
{
	Renderer::Renderer()
	{
#if ME_DIRECTX
		m_device = new D3D12Device();
		m_sceneRenderer = std::make_unique<TestModelRenderer>(static_cast<D3D12Device*>(m_device));
#elif ME_OPENGL
		m_device = new GLDevice();
#endif

#if ME_ENABLE_RENDERDOC
		RenderDoc = new RenderDocManager();
#endif
	}

	Renderer::~Renderer()
	{

	}

	void Renderer::SetWindow()
	{

	}

	void Renderer::RegisterDeviceNotify(IDeviceNotify* deviceNotify)
	{

	}

	IDevice& Renderer::GetDevice()
	{
		return *m_device;
	}

	void Renderer::Update(float dt)
	{
#if ME_DIRECTX
		m_timer.Tick([&]()
		{
			m_sceneRenderer->Update(m_timer);
		});
#endif
	}

	void Renderer::Render()
	{
		m_device->PreRender();

#if ME_DIRECTX
		if (m_timer.GetFrameCount() == 0)
		{
			return;
		}
		m_sceneRenderer->Render();
#endif
		for (FBXModel* model : Models)
		{
			model->Draw();
		}
		m_device->Present();
	}

	void Renderer::ReleaseDeviceDependentResources() const
	{
#if ME_DIRECTX
		m_sceneRenderer->ReleaseDeviceDependentResources();
#endif
	}

	void Renderer::CreateDeviceDependentResources() const
	{
#if ME_DIRECTX
		m_sceneRenderer->CreateDeviceDependentResources();
		m_sceneRenderer->CreateWindowSizeDependentResources();
#endif
	}

	void Renderer::PushModel(FBXModel* model)
	{
		Models.push_back(model);
	}

	ResourceCache& Renderer::GetResources()
	{
		return Resources;
	}

}
