#include "Renderer.h"
#include "Device/D3D12Device.h"
#include "Device/GLDevice.h"

#include "Utils/DirectXHelper.h"
#include <DirectXMath.h>
#include "Components/Camera.h"

using namespace DirectX;
using namespace Windows::Foundation;

namespace Moonlight
{
	Renderer::Renderer()
	{
#if ME_DIRECTX
		m_device = new D3D12Device();
#endif

#if ME_ENABLE_RENDERDOC
		RenderDoc = new RenderDocManager();
#endif

		CreateDeviceDependentResources();
	}

	Renderer::~Renderer()
	{
		ReleaseDeviceDependentResources();
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
	}

	void Renderer::Render()
	{
		Camera* currentCamera = Camera::CurrentCamera;
		if (!currentCamera)
		{
			return;
		}

		m_device->PreRender();

#if ME_DIRECTX
		auto device = static_cast<D3D12Device*>(m_device);
		auto context = device->GetD3DDeviceContext();

		TSize outputSize = device->GetOutputSize();
		float aspectRatio = outputSize.Width / outputSize.Height;
		float fovAngleY = 70.0f * XM_PI / 180.0f;

		// This is a simple example of change that can be made when the app is in
		// portrait or snapped view.
		if (aspectRatio < 1.0f)
		{
			fovAngleY *= 2.0f;
		}

		// Note that the OrientationTransform3D matrix is post-multiplied here
		// in order to correctly orient the scene to match the display orientation.
		// This post-multiplication step is required for any draw calls that are
		// made to the swap chain render target. For draw calls to other targets,
		// this transform should not be applied.

		// This sample makes use of a right-handed coordinate system using row-major matrices.
		XMMATRIX perspectiveMatrix = XMMatrixPerspectiveFovRH(
			fovAngleY,
			aspectRatio,
			0.01f,
			100.0f
		);

		XMFLOAT4X4 orientation = device->GetOrientationTransform3D();

		XMMATRIX orientationMatrix = XMLoadFloat4x4(&orientation);

		XMStoreFloat4x4(&m_constantBufferData.model, XMMatrixTranspose(XMMatrixIdentity()));

		XMStoreFloat4x4(
			&m_constantBufferData.projection,
			XMMatrixTranspose(perspectiveMatrix * orientationMatrix)
		);

		const XMVECTORF32 eye = { currentCamera->Position.x, currentCamera->Position.y, currentCamera->Position.z, 0 };
		const XMVECTORF32 at = { currentCamera->Position.x + currentCamera->Front.x, currentCamera->Position.y + currentCamera->Front.y, currentCamera->Position.z + currentCamera->Front.z, 0.0f };
		const XMVECTORF32 up = { currentCamera->Up.x, currentCamera->Up.y, currentCamera->Up.z, 0 };

		XMStoreFloat4x4(&m_constantBufferData.view, XMMatrixTranspose(XMMatrixLookAtRH(eye, at, up)));

		// Prepare the constant buffer to send it to the graphics device.
		context->UpdateSubresource1(
			m_constantBuffer.Get(),
			0,
			NULL,
			&m_constantBufferData,
			0,
			0,
			0
		);

		// Send the constant buffer to the graphics device.
		context->VSSetConstantBuffers1(
			0,
			1,
			m_constantBuffer.GetAddressOf(),
			nullptr,
			nullptr
		);
#endif
		for (FBXModel* model : Models)
		{
			model->Draw();
		}
		m_device->Present();
	}

	void Renderer::ReleaseDeviceDependentResources()
	{
		m_constantBuffer.Reset();
	}

	void Renderer::CreateDeviceDependentResources()
	{
		CD3D11_BUFFER_DESC constantBufferDesc(sizeof(ModelViewProjectionConstantBuffer), D3D11_BIND_CONSTANT_BUFFER);
		DX::ThrowIfFailed(
			static_cast<D3D12Device*>(m_device)->GetD3DDevice()->CreateBuffer(
				&constantBufferDesc,
				nullptr,
				&m_constantBuffer
			)
		);
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
