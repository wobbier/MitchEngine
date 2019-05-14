#include "Renderer.h"
#include "Device/D3D12Device.h"
#include "Device/GLDevice.h"

#include "Utils/DirectXHelper.h"
#include <DirectXMath.h>
#include "Components/Camera.h"
#include <DirectXColors.h>
#include "Graphics/SkyBox.h"
#include "optick.h"
#include "Graphics/Plane.h"
#include <functional>
#include "imgui.h"
#include "examples/imgui_impl_win32.h"
#include "examples/imgui_impl_dx11.h"
#include "Graphics/RenderTexture.h"

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

	void Renderer::UpdateMeshMatrix(unsigned int Id, DirectX::XMMATRIX NewTransform)
	{
		if (Id >= Meshes.size())
		{
			return;
		}

		Meshes[Id].Transform = std::move(NewTransform);
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

	void Renderer::Render(std::function<void()> func, const CameraData& mainCamera, const CameraData& editorCamera)
	{
		OPTICK_EVENT("Renderer::Render", Optick::Category::Rendering);

		auto context = m_device->GetD3DDeviceContext();

		delete RTT;
		delete RTT2;
			RTT = new RenderTexture(m_device);
			RTT2 = new RenderTexture(m_device);
		// Reset the viewport to target the whole screen.
		auto viewport = m_device->GetScreenViewport();

		context->RSSetViewports(1, &viewport);

		CD3D11_VIEWPORT m_screenViewport = CD3D11_VIEWPORT(
			0.0f,
			0.0f,
			mainCamera.OutputSize.X(),
			mainCamera.OutputSize.Y()
		);

		context->RSSetViewports(1, &m_screenViewport);
		float darkGrey = (57.0f / 255.0f);
		DirectX::XMVECTORF32 color = { {darkGrey, darkGrey, darkGrey, 1.0f} };
		// Clear the back buffer and depth stencil view.
		context->ClearRenderTargetView(m_device->GetBackBufferRenderTargetView(), color);
		context->ClearRenderTargetView(RTT->renderTargetViewMap, color);
		context->ClearRenderTargetView(RTT2->renderTargetViewMap, color);
		context->ClearDepthStencilView(m_device->GetDepthStencilView(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

		// Prepare the constant buffer to send it to the graphics device.
		light.dir = XMFLOAT4(0.25f, 0.5f, -1.0f, 1.0f);
		light.ambient = XMFLOAT4(0.2f, 0.2f, 0.2f, 1.0f);
		light.diffuse = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);

		//m_perFrameBufferData.light = light;
		context->UpdateSubresource1(m_perFrameBuffer.Get(), 0, NULL, &light, 0, 0, 0);
		context->PSSetConstantBuffers1(0, 1, m_perFrameBuffer.GetAddressOf(), nullptr, nullptr);

		RTT->Resize(mainCamera.OutputSize);
		RTT2->Resize(editorCamera.OutputSize);
		//m_device->GetD3DDeviceContext()->DiscardView1(m_device->GetDepthStencilView(), nullptr, 0);

		// Reset render targets to the screen.
#if ME_EDITOR
		ID3D11RenderTargetView *const targets[1] = { RTT->renderTargetViewMap };
#else
		ID3D11RenderTargetView *const targets[1] = { m_device->GetBackBufferRenderTargetView() };
#endif
		context->OMSetRenderTargets(1, targets, m_device->GetDepthStencilView());

		DrawScene(context, m_constantBufferData, mainCamera);

		context->ClearDepthStencilView(m_device->GetDepthStencilView(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

		CD3D11_VIEWPORT screenViewport = CD3D11_VIEWPORT(
			0.0f,
			0.0f,
			editorCamera.OutputSize.X(),
			editorCamera.OutputSize.Y()
		);

		context->RSSetViewports(1, &screenViewport);

		ID3D11RenderTargetView* const targets2[1] = { RTT2->renderTargetViewMap };
		context->OMSetRenderTargets(1, targets2, m_device->GetDepthStencilView());
		DrawScene(context, m_constantBufferSceneData, editorCamera);


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

		// Reset the viewport to target the whole screen.

		context->RSSetViewports(1, &viewport);
#if ME_EDITOR
		ID3D11RenderTargetView *const targets3[1] = { m_device->GetBackBufferRenderTargetView() };// , m_device->renderTargetViewMap
			//////////////////////////// Draw the Map
			// Make sure to set the render target back
		GetDevice().GetD3DDeviceContext()->OMSetRenderTargets(1, targets3, m_device->GetDepthStencilView());

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
		m_device->GetD3DDeviceContext()->DiscardView1(RTT->shaderResourceViewMap, nullptr, 0);
		m_device->GetD3DDeviceContext()->DiscardView1(RTT2->shaderResourceViewMap, nullptr, 0);

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


	void Renderer::DrawScene(ID3D11DeviceContext3* context, ModelViewProjectionConstantBuffer& constantBufferSceneData, const CameraData& camera)
	{
		Vector2 outputSize = camera.OutputSize;
		float aspectRatio = static_cast<float>(outputSize.X()) / static_cast<float>(outputSize.Y());
		float fovAngleY = camera.FOV * XM_PI / 180.0f;

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
			.1f,
			1000.0f
		);

		XMStoreFloat4x4(
			&constantBufferSceneData.projection,
			XMMatrixTranspose(perspectiveMatrix)
		);

		const XMVECTORF32 eye = { camera.Position.X(), camera.Position.Y(), camera.Position.Z(), 0 };
		const XMVECTORF32 at = { camera.Position.X() + camera.Front.X(), camera.Position.Y() + camera.Front.Y(), camera.Position.Z() + camera.Front.Z(), 0.0f };
		const XMVECTORF32 up = { camera.Up.X(), camera.Up.Y(), camera.Up.Z(), 0 };

		//XMStoreFloat4x4(&m_constantBufferData.view, XMMatrixTranspose(XMMatrixIdentity()));
		XMStoreFloat4x4(&constantBufferSceneData.view, XMMatrixTranspose(XMMatrixLookAtRH(eye, at, up)));

		{
			XMStoreFloat4x4(&constantBufferSceneData.model, XMMatrixTranspose(XMMatrixTranslation(eye[0], eye[1], eye[2])));
			// Prepare the constant buffer to send it to the graphics device.
			context->UpdateSubresource1(
				m_constantBuffer.Get(),
				0,
				NULL,
				&constantBufferSceneData,
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
		if (Lights.size() > 0)
		{
			//constantBufferSceneData.light = Lights[0];
		}

		{
			for (const ModelCommand& model : Models)
			{
				OPTICK_EVENT("Render::ModelCommand", Optick::Category::Rendering);
				XMStoreFloat4x4(&constantBufferSceneData.model, XMMatrixTranspose(model.Transform));
				// Prepare the constant buffer to send it to the graphics device.
				context->UpdateSubresource1(
					m_constantBuffer.Get(),
					0,
					NULL,
					&constantBufferSceneData,
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
					OPTICK_EVENT("Render::SingleMesh");

					//if (model.ModelShader != cachedShader)
					{
						cachedShader = model.ModelShader;
						cachedShader->Use();
					}

					mesh->Draw();
				}
			}
		}

		{
			for (const MeshCommand& model : Meshes)
			{
				OPTICK_EVENT("Render::ModelCommand", Optick::Category::Rendering);
				XMStoreFloat4x4(&constantBufferSceneData.model, XMMatrixTranspose(model.Transform));
				// Prepare the constant buffer to send it to the graphics device.
				context->UpdateSubresource1(
					m_constantBuffer.Get(),
					0,
					NULL,
					&constantBufferSceneData,
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

				{
					OPTICK_EVENT("Render::SingleMesh");

					model.MeshShader->Use();

					model.SingleMesh->Draw();
				}
			}
		}

		XMStoreFloat4x4(&constantBufferSceneData.model, XMMatrixTranspose(XMMatrixIdentity()));
		// Prepare the constant buffer to send it to the graphics device.
		context->UpdateSubresource1(
			m_constantBuffer.Get(),
			0,
			NULL,
			&constantBufferSceneData,
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

		//Grid->Draw(GetDevice());
	}

	void Renderer::ReleaseDeviceDependentResources()
	{
		m_constantBuffer.Reset();
		m_perFrameBuffer.Reset();
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

		CD3D11_BUFFER_DESC perFrameBufferDesc(sizeof(LightCommand), D3D11_BIND_CONSTANT_BUFFER);
		/*CD3D11_BUFFER_DESC perFrameBufferDesc;
		ZeroMemory(&perFrameBufferDesc, sizeof(D3D11_BUFFER_DESC));

		perFrameBufferDesc.Usage = D3D11_USAGE_DEFAULT;
		perFrameBufferDesc.ByteWidth = sizeof(LightCommand);
		perFrameBufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		perFrameBufferDesc.CPUAccessFlags = 0;
		perFrameBufferDesc.MiscFlags = 0;*/
		DX::ThrowIfFailed(
			static_cast<D3D12Device*>(m_device)->GetD3DDevice()->CreateBuffer(&perFrameBufferDesc, nullptr, &m_perFrameBuffer)
		);
		//hr = d3d11Device->CreateBuffer(&cbbd, NULL, &cbPerFrameBuffer);
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

	unsigned int Renderer::PushLight(const LightCommand& NewLight)
	{
		if (!FreeLightCommandIndicies.empty())
		{
			unsigned int openIndex = FreeLightCommandIndicies.front();
			FreeLightCommandIndicies.pop();
			Lights[openIndex] = std::move(NewLight);
			return openIndex;
		}

		Lights.push_back(std::move(NewLight));
		return static_cast<unsigned int>(Lights.size() - 1);
	}

	bool Renderer::PopLight(unsigned int id)
	{
		if (id >= Lights.size())
		{
			return false;
		}

		// Reset struct

		return true;
	}

	void Renderer::ClearLights()
	{
		Lights.clear();
		while (!FreeLightCommandIndicies.empty())
		{
			FreeLightCommandIndicies.pop();
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

	unsigned int Renderer::PushMesh(Moonlight::MeshCommand command)
	{
		if (!FreeMeshCommandIndicies.empty())
		{
			unsigned int openIndex = FreeMeshCommandIndicies.front();
			FreeMeshCommandIndicies.pop();
			Meshes[openIndex] = std::move(command);
			return openIndex;
		}

		Meshes.push_back(std::move(command));
		return static_cast<unsigned int>(Meshes.size() - 1);
	}

}
