#include "Renderer.h"
#include "Device/D3D12Device.h"
#include "Device/GLDevice.h"

#ifdef ME_PLATFORM_UWP
#include <DirectXColors.h>
#endif

namespace Moonlight
{
	Renderer::Renderer()
	{
#ifdef ME_PLATFORM_UWP
		m_device = new D3D12Device();
		m_sceneRenderer = std::make_unique<TestModelRenderer>(static_cast<D3D12Device*>(m_device));
#endif // ME_PLATFORM_UWP

#ifdef ME_PLATFORM_WIN64
		m_device = new GLDevice();
#endif // ME_PLATFORM_WIN64
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
#if ME_PLATFORM_UWP
		m_timer.Tick([&]()
		{
			m_sceneRenderer->Update(m_timer);
		});
#endif
	}

	void Renderer::Render()
	{
#if ME_PLATFORM_UWP
		if (m_timer.GetFrameCount() == 0)
		{
			return;
		}
		D3D12Device* device = static_cast<D3D12Device*>(m_device);

		auto context = device->GetD3DDeviceContext();

		// Reset the viewport to target the whole screen.
		auto viewport = device->GetScreenViewport();
		context->RSSetViewports(1, &viewport);

		// Reset render targets to the screen.
		ID3D11RenderTargetView *const targets[1] = { device->GetBackBufferRenderTargetView() };
		context->OMSetRenderTargets(1, targets, device->GetDepthStencilView());

		// Clear the back buffer and depth stencil view.
		context->ClearRenderTargetView(device->GetBackBufferRenderTargetView(), DirectX::Colors::Black);
		context->ClearDepthStencilView(device->GetDepthStencilView(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
#endif
		m_device->Present();
	}

	void Renderer::ReleaseDeviceDependentResources() const
	{
#if ME_PLATFORM_UWP
		m_sceneRenderer->ReleaseDeviceDependentResources();
#endif
	}

	void Renderer::CreateDeviceDependentResources() const
	{
#if ME_PLATFORM_UWP
		m_sceneRenderer->CreateDeviceDependentResources();
		m_sceneRenderer->CreateWindowSizeDependentResources();
#endif
	}

}
