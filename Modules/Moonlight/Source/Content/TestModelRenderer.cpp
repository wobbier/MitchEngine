#include "TestModelRenderer.h"

#if ME_DIRECTX

#include "Components/Camera.h"
#include "Device/D3D12Device.h"
#include "Utils/DirectXHelper.h"

using namespace DirectX;
using namespace Windows::Foundation;

namespace Moonlight
{
	// Loads vertex and pixel shaders from files and instantiates the cube geometry.
	TestModelRenderer::TestModelRenderer(D3D12Device* deviceResources) :
		m_loadingComplete(false),
		m_degreesPerSecond(45),
		m_tracking(false),
		m_deviceResources(deviceResources)
	{
		CreateDeviceDependentResources();
		CreateWindowSizeDependentResources();
	}

	// Initializes view parameters when the window size changes.
	void TestModelRenderer::CreateWindowSizeDependentResources()
	{

	}

	// Called once per frame, rotates the cube and calculates the model and view matrices.
	void TestModelRenderer::Update(DX::StepTimer const& timer)
	{
		if (!m_tracking)
		{
			// Convert degrees to radians, then convert seconds to rotation angle
			float radiansPerSecond = XMConvertToRadians(m_degreesPerSecond);
			double totalRotation = timer.GetTotalSeconds() * radiansPerSecond;
			float radians = static_cast<float>(fmod(totalRotation, XM_2PI));

			Rotate(radians);
		}
	}

	// Rotate the 3D cube model a set amount of radians.
	void TestModelRenderer::Rotate(float radians)
	{
		// Prepare to pass the updated model matrix to the shader
		XMStoreFloat4x4(&m_constantBufferData.model, XMMatrixTranspose(XMMatrixRotationY(radians)));
	}

	void TestModelRenderer::StartTracking()
	{
		m_tracking = true;
	}

	// When tracking, the 3D cube can be rotated around its Y axis by tracking pointer position relative to the output screen width.
	void TestModelRenderer::TrackingUpdate(float positionX)
	{
		if (m_tracking)
		{
			float radians = XM_2PI * 2.0f * positionX / m_deviceResources->GetOutputSize().Width;
			Rotate(radians);
		}
	}

	void TestModelRenderer::StopTracking()
	{
		m_tracking = false;
	}

	// Renders one frame using the vertex and pixel shaders.
	void TestModelRenderer::Render()
	{
		Camera* currentCamera = Camera::CurrentCamera;
		if (!currentCamera)
		{
			return;
		}

		TSize outputSize = m_deviceResources->GetOutputSize();
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

		XMFLOAT4X4 orientation = m_deviceResources->GetOrientationTransform3D();

		XMMATRIX orientationMatrix = XMLoadFloat4x4(&orientation);

		XMStoreFloat4x4(
			&m_constantBufferData.projection,
			XMMatrixTranspose(perspectiveMatrix * orientationMatrix)
		);

		static const XMVECTORF32 eye = { currentCamera->Position.x, currentCamera->Position.y, currentCamera->Position.z, 0 };// { 0.0f, 0.7f, 1.5f, 0.0f };//
		static const XMVECTORF32 at = { 0.0f, 0.f, 0.0f, 0.0f };
		static const XMVECTORF32 up =  { currentCamera->Up.x, currentCamera->Up.z, currentCamera->Up.y, 0 }; //{ 0.f, 1.0f, 0.f, 0.f };//

		XMStoreFloat4x4(&m_constantBufferData.view, XMMatrixTranspose(XMMatrixLookAtRH(eye, at, up)));

		auto context = m_deviceResources->GetD3DDeviceContext();

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
	}

	void TestModelRenderer::CreateDeviceDependentResources()
	{
		CD3D11_BUFFER_DESC constantBufferDesc(sizeof(ModelViewProjectionConstantBuffer), D3D11_BIND_CONSTANT_BUFFER);
		DX::ThrowIfFailed(
			m_deviceResources->GetD3DDevice()->CreateBuffer(
				&constantBufferDesc,
				nullptr,
				&m_constantBuffer
			)
		);
	}

	void TestModelRenderer::ReleaseDeviceDependentResources()
	{
		m_constantBuffer.Reset();
	}
}

#endif