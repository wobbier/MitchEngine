#include "Renderer.h"
#include "Device/D3D12Device.h"
#include "Device/GLDevice.h"

#include "Utils/DirectXHelper.h"
#include <DirectXMath.h>
#include "Components/Camera.h"
#include <DirectXColors.h>
#include "Graphics/SkyBox.h"
#include "Brofiler.h"
#include "Graphics/Plane.h"
#include <functional>
#include "imgui.h"
#include "examples/imgui_impl_win32.h"
#include "examples/imgui_impl_dx11.h"

using namespace DirectX;
using namespace Windows::Foundation;

namespace Moonlight
{
	void Renderer::UpdateMatrix(unsigned int Id, DirectX::XMMATRIX NewTransform)
	{
		if (Id >= Models.size())
		{
			return;
		}

		Models[Id].Transform = std::move(NewTransform);
	}

	Renderer::Renderer()
	{
		m_device = new D3D12Device();

#if ME_ENABLE_RENDERDOC
		RenderDoc = new RenderDocManager();
#endif

		CreateDeviceDependentResources();
	}

	Renderer::~Renderer()
	{
		ReleaseDeviceDependentResources();
	}

	void Renderer::Init()
	{
		Grid = new Plane("Assets/skybox.jpg", GetDevice());
		Sky = new SkyBox("Assets/skybox.jpg");

		D3D11_SAMPLER_DESC sampDesc;
		ZeroMemory(&sampDesc, sizeof(sampDesc));
		sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
		sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
		sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
		sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
		sampDesc.ComparisonFunc = D3D11_COMPARISON_LESS;
		sampDesc.MinLOD = 0;
		sampDesc.MaxLOD = D3D11_FLOAT32_MAX;
		m_device->GetD3DDevice()->CreateSamplerState(&sampDesc, &CubesTexSamplerState);
	}

	void Renderer::SetWindow()
	{

	}

	void Renderer::RegisterDeviceNotify(IDeviceNotify* deviceNotify)
	{

	}

	D3D12Device& Renderer::GetDevice()
	{
		return *m_device;
	}

	void Renderer::Update(float dt)
	{
	}

	void Renderer::Render(std::function<void()> func)
	{
		BROFILER_CATEGORY("Renderer::Render", Brofiler::Color::CornflowerBlue);

		Camera* currentCamera = Camera::CurrentCamera;
		if (!currentCamera)
		{
			return;
		}

		auto context = m_device->GetD3DDeviceContext();

		// Reset the viewport to target the whole screen.
		auto viewport = m_device->GetScreenViewport();
		context->RSSetViewports(1, &viewport);

		// Reset render targets to the screen.
#if ME_EDITOR
		ID3D11RenderTargetView *const targets[1] = { m_device->renderTargetViewMap };//
#else
		ID3D11RenderTargetView *const targets[1] = { m_device->GetBackBufferRenderTargetView() };//
#endif
		context->OMSetRenderTargets(1, targets, m_device->GetDepthStencilView());

		float darkGrey = (57.0f / 255.0f);
		DirectX::XMVECTORF32 color = { {darkGrey, darkGrey, darkGrey, 1.0f} };
		// Clear the back buffer and depth stencil view.
		context->ClearRenderTargetView(m_device->GetBackBufferRenderTargetView(), color);
		context->ClearRenderTargetView(m_device->renderTargetViewMap, color);
		context->ClearDepthStencilView(m_device->GetDepthStencilView(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

		Vector2 outputSize = m_device->GetOutputSize();
		float aspectRatio = static_cast<float>(outputSize.X()) / static_cast<float>(outputSize.Y());
		float fovAngleY = currentCamera->GetFOV() * XM_PI / 180.0f;

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
			1.f,
			100.0f
		);

		XMStoreFloat4x4(
			&m_constantBufferData.projection,
			XMMatrixTranspose(perspectiveMatrix)
		);

		const XMVECTORF32 eye = { currentCamera->Position.X(), currentCamera->Position.Y(), currentCamera->Position.Z(), 0 };
		const XMVECTORF32 at = { currentCamera->Position.X() + currentCamera->Front.X(), currentCamera->Position.Y() + currentCamera->Front.Y(), currentCamera->Position.Z() + currentCamera->Front.Z(), 0.0f };
		const XMVECTORF32 up = { currentCamera->Up.X(), currentCamera->Up.Y(), currentCamera->Up.Z(), 0 };

		//XMStoreFloat4x4(&m_constantBufferData.view, XMMatrixTranspose(XMMatrixIdentity()));
		XMStoreFloat4x4(&m_constantBufferData.view, XMMatrixTranspose(XMMatrixLookAtRH(eye, at, up)));

		{
			XMStoreFloat4x4(&m_constantBufferData.model, XMMatrixTranspose(XMMatrixTranslation(eye[0], eye[1], eye[2])));
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
			Sky->Draw();

			m_device->ResetCullingMode();
		}

		{
			for (const ModelCommand& model : Models)
			{
				BROFILER_CATEGORY("Render::ModelCommand", Brofiler::Color::OrangeRed);
				XMStoreFloat4x4(&m_constantBufferData.model, XMMatrixTranspose(model.Transform));
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
				Shader* cachedShader = model.ModelShader;
				for (Mesh* mesh : model.Meshes)
				{
					BROFILER_CATEGORY("Render::SingleMesh", Brofiler::Color::OrangeRed);

					//if (model.ModelShader != cachedShader)
					{
						cachedShader = model.ModelShader;
						cachedShader->Use();
					}

					mesh->Draw();
				}
			}
		}
		XMStoreFloat4x4(&m_constantBufferData.model, XMMatrixTranspose(XMMatrixIdentity()));
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

		Grid->Draw(GetDevice());

		//UINT stride = sizeof(Vertex);
		//UINT offset = 0;
		//
		// Set it to the D2D_PS so that we do not impliment lighting
		//GetDevice().GetD3DDeviceContext()->PSSetShader(D2D_PS, 0, 0);

		////Set the d2d square's Index buffer
		//GetDevice().GetD3DDeviceContext()->IASetIndexBuffer(m_device->d2dIndexBuffer, DXGI_FORMAT_R32_UINT, 0);
		////Set the d2d square's vertex buffer
		//GetDevice().GetD3DDeviceContext()->IASetVertexBuffers(0, 1, &m_device->d2dVertBuffer, &stride, &offset);

		//// Just set the WVP to a scale and translate, which will put the square into the bottom right corner of the screen
		////m_constantBufferData. = XMMatrixScaling(0.5f, 0.5f, 0.0f) * XMMatrixTranslation(0.5f, -0.5f, 0.0f);
		//XMStoreFloat4x4(&m_constantBufferData.model, XMMatrixTranspose(XMMatrixTranslation(0.5f, -0.5f, 0.0f)));
		////XMMatrixTranspose(WVP);
		//GetDevice().GetD3DDeviceContext()->UpdateSubresource(m_constantBuffer.Get(), 0, NULL, &m_constantBufferData, 0, 0);
		//GetDevice().GetD3DDeviceContext()->VSSetConstantBuffers(0, 1, m_constantBuffer.GetAddressOf());
		//GetDevice().GetD3DDeviceContext()->PSSetShaderResources(0, 1, &m_device->shaderResourceViewMap);    // Draw the map to the square

		//GetDevice().GetD3DDeviceContext()->PSSetSamplers(0, 1, &CubesTexSamplerState);

		////GetDevice().GetD3DDeviceContext()->RSSetState(CWcullMode);
		////Draw the second cube
		//GetDevice().GetD3DDeviceContext()->DrawIndexed(6, 0, 0);
		
#if ME_EDITOR
		ID3D11RenderTargetView *const targets2[1] = { m_device->GetBackBufferRenderTargetView() };// , m_device->renderTargetViewMap
			//////////////////////////// Draw the Map
			// Make sure to set the render target back
		GetDevice().GetD3DDeviceContext()->OMSetRenderTargets(1, targets2, m_device->GetDepthStencilView());

#endif
		func();
		// The first argument instructs DXGI to block until VSync, putting the application
		// to sleep until the next VSync. This ensures we don't waste any cycles rendering
		// frames that will never be displayed to the screen.
		DXGI_PRESENT_PARAMETERS parameters = { 0 };
		HRESULT hr = m_device->GetSwapChain()->Present1(0, 0, &parameters);

		// Discard the contents of the render target.
		// This is a valid operation only when the existing contents will be entirely
		// overwritten. If dirty or scroll rects are used, this call should be removed.
		m_device->GetD3DDeviceContext()->DiscardView1(m_device->GetBackBufferRenderTargetView(), nullptr, 0);
		m_device->GetD3DDeviceContext()->DiscardView1(m_device->shaderResourceViewMap, nullptr, 0);

		// Discard the contents of the depth stencil.
		m_device->GetD3DDeviceContext()->DiscardView1(m_device->GetDepthStencilView(), nullptr, 0);

		// If the device was removed either by a disconnection or a driver upgrade, we 
		// must recreate all device resources.
		if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET)
		{
			m_device->HandleDeviceLost();
		}
		else
		{
			DX::ThrowIfFailed(hr);
		}
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

	unsigned int Renderer::PushModel(const ModelCommand& NewModel)
	{
		if (!FreeCommandIndicies.empty())
		{
			unsigned int openIndex = FreeCommandIndicies.front();
			FreeCommandIndicies.pop();
			Models[openIndex] = std::move(NewModel);
			return openIndex;
		}

		Models.push_back(std::move(NewModel));
		return static_cast<unsigned int>(Models.size() - 1);
	}

	bool Renderer::PopModel(unsigned int id)
	{
		if (id >= Models.size())
		{
			return false;
		}

		Models[id].Meshes.clear();
		return true;
	}

	void Renderer::ClearModels()
	{
		Models.clear();
		while (!FreeCommandIndicies.empty())
		{
			FreeCommandIndicies.pop();
		}
	}

	void Renderer::WindowResized(const Vector2& NewSize)
	{
		if (!m_device)
		{
			return;
		}

		m_device->WindowSizeChanged(NewSize);
	}

}
