#include "Renderer.h"
#include "Device/DX11Device.h"
#include "Device/GLDevice.h"

#include "Utils/DirectXHelper.h"
#include <DirectXMath.h>
#include "Components/Camera.h"
#include <DirectXColors.h>
#include "Graphics/SkyBox.h"
#include "optick.h"
#include <functional>
#include "imgui.h"
#include "examples/imgui_impl_win32.h"
#include "examples/imgui_impl_dx11.h"
#include "Graphics/RenderTexture.h"
#include "Engine/Input.h"
#include <GeometricPrimitive.h>
#include "Utils/StringUtils.h"
#include <SimpleMath.h>
#include <wrl/client.h>
#include "Mathf.h"
#include "Graphics/MeshData.h"
#include "Math/Vector2.h"
#include "Math/Frustrum.h"

using namespace DirectX;
using namespace Windows::Foundation;

namespace Moonlight
{
	void Renderer::UpdateMatrix(unsigned int Id, DirectX::SimpleMath::Matrix NewTransform)
	{
		if (Id >= DebugColliders.size())
		{
			return;
		}

		DebugColliders[Id].Transform = NewTransform;
	}

	void Renderer::UpdateMeshMatrix(unsigned int Id, DirectX::SimpleMath::Matrix NewTransform)
	{
		if (Id >= Meshes.size())
		{
			return;
		}

		Meshes[Id].Transform = NewTransform;
	}

	void Renderer::UpdateCamera(unsigned int Id, CameraData& NewCommand)
	{
		if (Id >= Cameras.size())
		{
			return;
		}

		Cameras[Id] = NewCommand;
	}

	Moonlight::CameraData& Renderer::GetCamera(unsigned int Id)
	{
		if (Id >= Cameras.size())
		{
			return Cameras[0];
		}

		return Cameras[Id];
	}

	Renderer::Renderer()
	{
		m_device = new DX11Device();

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
		m_defaultSampler = m_device->CreateSamplerState(D3D11_FILTER_ANISOTROPIC, D3D11_TEXTURE_ADDRESS_WRAP);
		m_computeSampler = m_device->CreateSamplerState(D3D11_FILTER_MIN_MAG_MIP_LINEAR, D3D11_TEXTURE_ADDRESS_WRAP);
/*
		m_tonemapProgram = m_device->CreateShaderProgram(
			m_device->CompileShader(Path("Assets/Shaders/ToneMap.hlsl"), "main_vs", "vs_5_0"),
			m_device->CompileShader(Path("Assets/Shaders/ToneMap.hlsl"), "main_ps", "ps_5_0"),
			nullptr
		);
*/
		m_font = std::make_unique<DirectX::SpriteFont>(m_device->GetD3DDevice(), StringUtils::ToWString(Path("Assets/Fonts/Comfortaa.spritefont").FullPath).c_str());
		m_spriteBatch = std::make_unique<DirectX::SpriteBatch>(m_device->GetD3DDeviceContext());
		shape = DirectX::GeometricPrimitive::CreateBox(m_device->GetD3DDeviceContext(), XMFLOAT3(1.f, 1.f, 1.f));
		primitiveBatch = std::make_unique<DirectX::PrimitiveBatch<VertexPositionTexCoord>>(m_device->GetD3DDeviceContext());
		//m_depthProgram = ShaderCommand("Assets/Shaders/DepthPass.hlsl");
		ResizeBuffers();

		// Prepare the constant buffer to send it to the graphics device.
		Sunlight.pos = XMFLOAT4(0.25f, 5.f, -1.0f, 1.0f);
		Sunlight.dir = XMFLOAT4(0.25f, 0.5f, -1.0f, 1.0f);
		Sunlight.ambient = XMFLOAT4(0.2f, 0.2f, 0.2f, 1.0f);
		Sunlight.diffuse = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);

		static const std::vector<D3D11_INPUT_ELEMENT_DESC> vertexDesc =
		{
			{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		};

		m_lightingProgram = m_device->CreateShaderProgram("Assets/Shaders/LightingPass.hlsl",
			m_device->CompileShader(Path("Assets/Shaders/LightingPass.hlsl"), "main_vs", "vs_4_0_level_9_3"),
			m_device->CompileShader(Path("Assets/Shaders/LightingPass.hlsl"), "main_ps", "ps_4_0_level_9_3"),
			&vertexDesc
		);


		static const std::vector<D3D11_INPUT_ELEMENT_DESC> vertexDesc2 =
		{
			{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		};

		m_depthProgram = m_device->CreateShaderProgram("Assets/Shaders/DepthPass.hlsl",
			m_device->CompileShader(Path("Assets/Shaders/DepthPass.hlsl"), "main_vs", "vs_4_0_level_9_3"),
			m_device->CompileShader(Path("Assets/Shaders/DepthPass.hlsl"), "main_ps", "ps_4_0_level_9_3"),
			&vertexDesc2
		);

		m_pickingShader = m_device->CreateShaderProgram("Assets/Shaders/PickingShader.hlsl",
			m_device->CompileShader(Path("Assets/Shaders/PickingShader.hlsl"), "main_vs", "vs_4_0_level_9_3"),
			m_device->CompileShader(Path("Assets/Shaders/PickingShader.hlsl"), "main_ps", "ps_4_0_level_9_3"),
			&vertexDesc2
		);

		{
			m_planeVerticies.clear();
			m_planeVerticies.reserve(4);
			{
				// top right
				Vertex tex;
				tex.Normal = Vector3::Up.GetInternalVec();
				tex.Position = Vector3(-0.5f, .5f, 0.f).GetInternalVec();
				tex.TextureCoord = Vector2(1.f, 0.f).GetInternalVec();
				m_planeVerticies.push_back(tex);
			}
			{
				// bottom right
				Vertex tex;
				tex.Normal = Vector3::Up.GetInternalVec();
				tex.Position = Vector3(.5f, .5f, 0.f).GetInternalVec();
				tex.TextureCoord = Vector2(1.f, 1.f).GetInternalVec();
				m_planeVerticies.push_back(tex);
			}
			{
				// bottom left
				Vertex tex;
				tex.Normal = Vector3::Up.GetInternalVec();
				tex.Position = Vector3(-0.5f, .5f, 0.f).GetInternalVec();
				tex.TextureCoord = Vector2(0.f, 1.f).GetInternalVec();
				m_planeVerticies.push_back(tex);
			}
			{
				// top left 
				Vertex tex;
				tex.Normal = Vector3::Up.GetInternalVec();
				tex.Position = Vector3(-0.5f, -0.5f, 0.f).GetInternalVec();
				tex.TextureCoord = Vector2(0.f, 0.f).GetInternalVec();
				m_planeVerticies.push_back(tex);
			}

			m_planeIndicies.clear();
			m_planeIndicies.reserve(6);

			m_planeIndicies = {0, 1, 3, 1, 2, 3};
		}
		{

			std::vector<Vertex> vertices =
			{
				// Front Face
				/* bottom left */Vertex{{-1.0f, -1.0f, -1.0f},{-1.0f, -1.0f, -1.0f}, {0.0f, 1.0f},{-1.0f, -1.0f, -1.0f},{-1.0f, -1.0f, -1.0f}},
				/* top left */ Vertex{{-1.0f,  1.0f, -1.0f},{-1.0f, -1.0f, -1.0f}, {0.0f, 0.0f},{-1.0f, -1.0f, -1.0f},{-1.0f, -1.0f, -1.0f}},
				/* top right */Vertex{{1.0f,  1.0f, -1.0f},{-1.0f, -1.0f, -1.0f}, {1.0f, 0.0f },{-1.0f, -1.0f, -1.0f},{-1.0f, -1.0f, -1.0f}},
				/* bottom right */Vertex{{1.0f, -1.0f, -1.0f},{-1.0f, -1.0f, -1.0f}, {1.0f, 1.0f },{-1.0f, -1.0f, -1.0f},{-1.0f, -1.0f, -1.0f}},
			};

			m_planeVerticies = vertices;

			std::vector<uint16_t> indices = {
				// Front Face
				0,  1,  2,
				0,  2,  3,
			};

			m_planeIndicies = indices;

			PlaneMesh = new MeshData(m_planeVerticies, m_planeIndicies);
		}
	}

	void Renderer::ResizeBuffers()
	{
		delete SceneViewRTT;
		SceneViewRTT = m_device->CreateFrameBuffer(m_device->GetOutputSize().X(), m_device->GetOutputSize().Y(), 1, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_D24_UNORM_S8_UINT);

		for (CameraData& cam : Cameras)
		{
			delete cam.Buffer;
			if (cam.IsMain)
			{
				GameViewRTT = cam.Buffer = m_device->CreateFrameBuffer(m_device->GetOutputSize().X(), m_device->GetOutputSize().Y(), 1, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_D24_UNORM_S8_UINT);
			}
			else
			{
				cam.Buffer = m_device->CreateFrameBuffer(m_device->GetOutputSize().X(), m_device->GetOutputSize().Y(), 1, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_D24_UNORM_S8_UINT);
			}
			
		}
	}

	void Renderer::SetWindow()
	{

	}

	void Renderer::RegisterDeviceNotify(IDeviceNotify* deviceNotify)
	{

	}

	DX11Device& Renderer::GetDevice()
	{
		return *m_device;
	}

	void Renderer::Update(float dt)
	{
	}

	void Renderer::Render(std::function<void()> func, std::function<void()> uiRender, const CameraData& editorCamera)
	{
		OPTICK_EVENT("Renderer::Render", Optick::Category::Rendering);

		auto context = m_device->GetD3DDeviceContext();

		//float darkGrey = (0.0f / 255.0f);
		DirectX::XMVECTORF32 color = DirectX::Colors::Black;// SimpleMath::Color(mainCamera.ClearColor.GetInternalVec());//{ {darkGrey, darkGrey, darkGrey, 1.0f} };

		// Clear the back buffer and depth stencil view.
		context->ClearRenderTargetView(m_device->GetBackBufferRenderTargetView(), color);

		for (int i = 0; i < Cameras.size(); ++i)
		{
			DirectX::SimpleMath::Color clearColor = DirectX::SimpleMath::Color(Cameras[i].ClearColor.GetInternalVec());
			FrameBuffer* buffer = Cameras[i].Buffer;
			if (buffer)
			{
				context->ClearRenderTargetView(buffer->RenderTargetView.Get(), color);
				context->ClearRenderTargetView(buffer->ColorRenderTargetView.Get(), clearColor);
				context->ClearRenderTargetView(buffer->NormalRenderTargetView.Get(), color);
				context->ClearRenderTargetView(buffer->PositionRenderTargetView.Get(), color);
				context->ClearRenderTargetView(buffer->SpecularRenderTargetView.Get(), color);
				context->ClearRenderTargetView(buffer->ShadowMapRenderTargetView.Get(), color);
				context->ClearRenderTargetView(buffer->UIRenderTargetView.Get(), DirectX::Colors::Transparent);
				context->ClearRenderTargetView(buffer->PickingTargetView.Get(), color);
				context->ClearDepthStencilView(buffer->DepthStencilView.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
			}
		}

		context->ClearRenderTargetView(SceneViewRTT->RenderTargetView.Get(), color);
		context->ClearRenderTargetView(SceneViewRTT->ColorRenderTargetView.Get(), color);
		context->ClearRenderTargetView(SceneViewRTT->NormalRenderTargetView.Get(), color);
		context->ClearRenderTargetView(SceneViewRTT->PositionRenderTargetView.Get(), color);
		context->ClearRenderTargetView(SceneViewRTT->SpecularRenderTargetView.Get(), color);
		context->ClearRenderTargetView(SceneViewRTT->ShadowMapRenderTargetView.Get(), color);

		//ID3D11RenderTargetView* nullViews[] = { nullptr, nullptr, nullptr, nullptr };
		//context->OMSetRenderTargets(ARRAYSIZE(nullViews), nullViews, nullptr);
		if (m_pickingRequested)
		{
			context->ClearRenderTargetView(SceneViewRTT->PickingTargetView.Get(), DirectX::Colors::Transparent);

			static PickingConstantBuffer buf2;
			DrawPickingTexture(m_device->GetD3DDeviceContext(), buf2, editorCamera, SceneViewRTT);
			SaveTextureToBmp(StringUtils::ToWString(Path("Assets/EXPORT.bmp").FullPath).c_str(), SceneViewRTT->PickingTexture.Get(), editorCamera, pickingLocation);

			m_device->GetD3DDeviceContext()->DiscardView1(SceneViewRTT->PickingResourceView.Get(), nullptr, 0);

			m_pickingRequested = false;
		}
		//static DepthPassBuffer buf;
		//DrawDepthOnlyScene(m_device->GetD3DDeviceContext(), buf, GameViewRTT);
		//DrawDepthOnlyScene(m_device->GetD3DDeviceContext(), buf, SceneViewRTT);

		uiRender();

		//CD3D11_VIEWPORT gameViewport = CD3D11_VIEWPORT(
		//	0.0f,
		//	0.0f,
		//	mainCamera.OutputSize.X(),
		//	mainCamera.OutputSize.Y()
		//);
		//context->RSSetViewports(1, &gameViewport);

		//m_device->GetD3DDeviceContext()->OMSetBlendState(0, 0, 0xffffffff);

		//// Reset render targets to the screen.
		//DrawScene(context, m_constantBufferData, mainCamera, mainCamera.Buffer, mainCamera.Buffer);

		for (CameraData& data : Cameras)
		{
			if (!data.IsMain)
			{
				CD3D11_VIEWPORT gameViewport = CD3D11_VIEWPORT(
					0.0f,
					0.0f,
					data.OutputSize.X(),
					data.OutputSize.Y()
				);
				context->RSSetViewports(1, &gameViewport);

				m_device->GetD3DDeviceContext()->OMSetBlendState(0, 0, 0xffffffff);

				// Reset render targets to the screen.
				DrawScene(context, m_constantBufferData, data, data.Buffer);
			}
		}

		for (CameraData& data : Cameras)
		{
			if (data.IsMain)
			{
				CD3D11_VIEWPORT gameViewport = CD3D11_VIEWPORT(
					0.0f,
					0.0f,
					data.OutputSize.X(),
					data.OutputSize.Y()
				);
				context->RSSetViewports(1, &gameViewport);

				m_device->GetD3DDeviceContext()->OMSetBlendState(0, 0, 0xffffffff);

				// Reset render targets to the screen.
				DrawScene(context, m_constantBufferData, data, data.Buffer);
				break;
			}
		}


#if ME_EDITOR
		context->ClearDepthStencilView(SceneViewRTT->DepthStencilView.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

		m_device->GetD3DDeviceContext()->OMSetBlendState(0, 0, 0xffffffff);
		//ID3D11RenderTargetView* const targets2[1] = { SceneViewRTT->renderTargetViewMap };
		DrawScene(context, m_constantBufferSceneData, editorCamera, SceneViewRTT);

		GetDevice().GetD3DDeviceContext()->OMSetRenderTargets(1, m_device->m_d3dRenderTargetView.GetAddressOf(), nullptr);
#endif

		func();

		GetDevice().GetD3DDeviceContext()->OMSetRenderTargets(1, m_device->m_d3dRenderTargetView.GetAddressOf(), nullptr);
		// The first argument instructs DXGI to block until VSync, putting the application
		// to sleep until the next VSync. This ensures we don't waste any cycles rendering
		// frames that will never be displayed to the screen.
		HRESULT hr;
		{
			OPTICK_EVENT("Present", Optick::Category::Rendering);

			DXGI_PRESENT_PARAMETERS parameters = { 0 };
			hr = m_device->GetSwapChain()->Present1(0, 0, &parameters);
		}

		// Discard the contents of the render target.
		// This is a valid operation only when the existing contents will be entirely
		// overwritten. If dirty or scroll rects are used, this call should be removed.
		m_device->GetD3DDeviceContext()->DiscardView1(m_device->GetBackBufferRenderTargetView(), nullptr, 0);

		for (int i = 0; i < Cameras.size(); ++i)
		{
			FrameBuffer* buffer = Cameras[i].Buffer;
			if (buffer)
			{
				m_device->GetD3DDeviceContext()->DiscardView1(buffer->ShaderResourceView.Get(), nullptr, 0);
				m_device->GetD3DDeviceContext()->DiscardView1(buffer->ColorShaderResourceView.Get(), nullptr, 0);
				m_device->GetD3DDeviceContext()->DiscardView1(buffer->NormalShaderResourceView.Get(), nullptr, 0);
				m_device->GetD3DDeviceContext()->DiscardView1(buffer->SpecularShaderResourceView.Get(), nullptr, 0);
				m_device->GetD3DDeviceContext()->DiscardView1(buffer->UIShaderResourceView.Get(), nullptr, 0);
				m_device->GetD3DDeviceContext()->DiscardView1(buffer->DepthStencilView.Get(), nullptr, 0);
			}
		}
		m_device->GetD3DDeviceContext()->DiscardView1(SceneViewRTT->ShaderResourceView.Get(), nullptr, 0);
		m_device->GetD3DDeviceContext()->DiscardView1(SceneViewRTT->ColorShaderResourceView.Get(), nullptr, 0);
		m_device->GetD3DDeviceContext()->DiscardView1(SceneViewRTT->NormalShaderResourceView.Get(), nullptr, 0);
		m_device->GetD3DDeviceContext()->DiscardView1(SceneViewRTT->SpecularShaderResourceView.Get(), nullptr, 0);

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


	void Renderer::DrawScene(ID3D11DeviceContext3* context, ModelViewProjectionConstantBuffer& constantBufferSceneData, const CameraData& camera, FrameBuffer* ViewRTT)
	{
		Vector2 outputSize = camera.OutputSize;
		if (outputSize.IsZero())
		{
			return;
		}
		float aspectRatio = static_cast<float>(outputSize.X()) / static_cast<float>(outputSize.Y());
		float fovAngleY = camera.FOV * XM_PI / 180.0f;

		if (!ViewRTT)
		{
			return;
		}

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

		m_device->GetD3DDeviceContext()->OMSetDepthStencilState(m_device->DepthStencilState, 0);
		if (camera.Projection == ProjectionType::Perspective)
		{
			// This sample makes use of a right-handed coordinate system using row-major matrices.
			XMMATRIX perspectiveMatrix = XMMatrixPerspectiveFovRH(
				fovAngleY,
				aspectRatio,
				camera.Near,
				camera.Far
			);

			XMStoreFloat4x4(
				&constantBufferSceneData.projection,
				XMMatrixTranspose(perspectiveMatrix)
			);
		}
		else if (camera.Projection == ProjectionType::Orthographic)
		{
			XMMATRIX orthographicMatrix = XMMatrixOrthographicRH(
				outputSize.X() / camera.OrthographicSize,
				outputSize.Y() / camera.OrthographicSize,
				camera.Near,
				camera.Far
			);

			XMStoreFloat4x4(
				&constantBufferSceneData.projection,
				(orthographicMatrix)
			);
		}

		const XMVECTORF32 eye = { camera.Position.X(), camera.Position.Y(), camera.Position.Z(), 0 };
		const XMVECTORF32 at = { /*camera.Position.X() + */camera.Front.X(),/* camera.Position.Y() + */camera.Front.Y(), /*camera.Position.Z() + */camera.Front.Z(), 0.0f };
		const XMVECTORF32 up = { camera.Up.X(), camera.Up.Y(), camera.Up.Z(), 0 };

		Sunlight.cameraPos = { camera.Position.X(), camera.Position.Y(), camera.Position.Z(), 0 };

		ID3D11RenderTargetView** targets[4] = { ViewRTT->ColorRenderTargetView.GetAddressOf(), ViewRTT->NormalRenderTargetView.GetAddressOf(), ViewRTT->SpecularRenderTargetView.GetAddressOf(), ViewRTT->PositionRenderTargetView.GetAddressOf() };
		context->OMSetRenderTargets(4, *targets, ViewRTT->DepthStencilView.Get());

		CD3D11_VIEWPORT screenViewport = CD3D11_VIEWPORT(
			0.0f,
			0.0f,
			camera.OutputSize.X(),
			camera.OutputSize.Y()
		);

		context->RSSetViewports(1, &screenViewport);

		XMStoreFloat2(&constantBufferSceneData.ViewportSize, camera.OutputSize.GetInternalVec());

		//XMStoreFloat4x4(&m_constantBufferData.view, XMMatrixTranspose(XMMatrixIdentity()));
		XMStoreFloat4x4(&constantBufferSceneData.view, XMMatrixTranspose(XMMatrixLookToRH(eye, at, up)));

		if (camera.Skybox)
		{
			XMStoreFloat4x4(&constantBufferSceneData.model, XMMatrixTranslation(eye[0], eye[1], eye[2]));
			XMStoreFloat4x4(&constantBufferSceneData.modelInv, XMMatrixInverse(nullptr, XMMatrixTranslation(eye[0], eye[1], eye[2])));
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
			camera.Skybox->Draw();
		}

		m_device->ResetCullingMode();

		//camera.CameraFrustum->TransformFrustum(constantBufferSceneData.projection, constantBufferSceneData.view);

		if (Lights.size() > 0)
		{
			//constantBufferSceneData.light = Lights[0];
		}

		{
			std::vector<MeshCommand> transparentMeshes;
			for (int i = 0; i < Meshes.size(); ++i)
			{
				MeshCommand& mesh = Meshes[i];
				if (mesh.MeshMaterial && mesh.MeshMaterial->IsTransparent())
				{
					transparentMeshes.push_back(mesh);
					continue;
				}
				DirectX::SimpleMath::Vector3 position;
				mesh.Transform.Decompose(DirectX::SimpleMath::Vector3(), DirectX::SimpleMath::Quaternion(), position);
				//DirectX::XMMatrixDecompose(nullptr, nullptr, &position, mesh.Transform);
// 
// 				if (!camera.CameraFrustum->IsInside(Vector3(position)))
// 				{
// 					continue;
// 				}

				if (mesh.SingleMesh && mesh.MeshMaterial)
				{
					OPTICK_EVENT("Render::ModelCommand", Optick::Category::Rendering);
					XMStoreFloat4x4(&constantBufferSceneData.model, mesh.Transform);
					XMStoreFloat4x4(&constantBufferSceneData.modelInv, XMMatrixInverse(nullptr, mesh.Transform));
					if (mesh.MeshMaterial->GetTexture(TextureType::Normal))
					{
						constantBufferSceneData.HasNormalMap = TRUE;
					}
					else
					{
						constantBufferSceneData.HasNormalMap = FALSE;
					}
					if (mesh.MeshMaterial->GetTexture(TextureType::Specular))
					{
						constantBufferSceneData.HasSpecMap = TRUE;
					}
					else
					{
						constantBufferSceneData.HasSpecMap = FALSE;
					}
					constantBufferSceneData.HasAlphaMap = FALSE;
					constantBufferSceneData.DiffuseColor = mesh.MeshMaterial->DiffuseColor.GetInternalVec();

					//constantBufferSceneData.Tiling = DirectX::SimpleMath::Vector2(&mesh.MeshMaterial->Tiling.GetInternalVec().x);
					XMStoreFloat2(&constantBufferSceneData.Tiling, mesh.MeshMaterial->Tiling.GetInternalVec());
					//constantBufferSceneData.Tiling = mesh.MeshMaterial->Tiling.GetInternalVec();
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
					context->PSSetConstantBuffers1(
						0,
						1,
						m_constantBuffer.GetAddressOf(),
						nullptr,
						nullptr
					);
					OPTICK_EVENT("Render::SingleMesh");

					mesh.MeshMaterial->MeshShader.Use();

					mesh.SingleMesh->Draw(mesh.MeshMaterial);

					//shape->Draw(XMMatrixTranspose(XMMatrixIdentity()), DirectX::XMLoadFloat4x4(&constantBufferSceneData.view), DirectX::XMLoadFloat4x4(&constantBufferSceneData.projection), Colors::Gray, nullptr, true);
				}
			}
			if (camera.Projection == ProjectionType::Perspective)
			{
				/*for (const DebugColliderCommand& collider : DebugColliders)
				{
					shape->Draw(collider.Transform, DirectX::XMLoadFloat4x4(&constantBufferSceneData.view), DirectX::XMLoadFloat4x4(&constantBufferSceneData.projection), Colors::Gray, nullptr, true);
				}*/
			}
			m_device->ResetCullingMode();
			m_device->GetD3DDeviceContext()->OMSetBlendState(m_device->TransparentBlendState, Colors::Black, 0xffffffff);

			for (const MeshCommand& mesh : transparentMeshes)
			{
				if (mesh.SingleMesh && mesh.MeshMaterial)
				{
					OPTICK_EVENT("Render::ModelCommand", Optick::Category::Rendering);
					XMStoreFloat4x4(&constantBufferSceneData.model, mesh.Transform);
					if (mesh.MeshMaterial->GetTexture(TextureType::Normal))
					{
						constantBufferSceneData.HasNormalMap = TRUE;
					}
					else
					{
						constantBufferSceneData.HasNormalMap = FALSE;
					}
					if (mesh.MeshMaterial->GetTexture(TextureType::Specular))
					{
						constantBufferSceneData.HasSpecMap = TRUE;
					}
					else
					{
						constantBufferSceneData.HasSpecMap = FALSE;
					}
					constantBufferSceneData.HasAlphaMap = TRUE;
					constantBufferSceneData.DiffuseColor = mesh.MeshMaterial->DiffuseColor.GetInternalVec();
					//constantBufferSceneData.Tiling = DirectX::SimpleMath::Vector2(&mesh.MeshMaterial->Tiling.GetInternalVec().x);
					//constantBufferSceneData.Tiling = mesh.MeshMaterial->Tiling.GetInternalVec();
					XMStoreFloat2(&constantBufferSceneData.Tiling, mesh.MeshMaterial->Tiling.GetInternalVec());
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
					context->PSSetConstantBuffers1(
						0,
						1,
						m_constantBuffer.GetAddressOf(),
						nullptr,
						nullptr
					);
					OPTICK_EVENT("Render::SingleAlphaMesh");

					mesh.MeshMaterial->MeshShader.Use();

					mesh.SingleMesh->Draw(mesh.MeshMaterial);
				}
			}
		}
		m_device->GetD3DDeviceContext()->OMSetBlendState(0, 0, 0xffffffff);

		CD3D11_VIEWPORT finalGameRenderViewport = CD3D11_VIEWPORT(
			0.0f,
			0.0f,
			ViewRTT->Width,
			ViewRTT->Height
		);
		LightingPassBuffer.Light = Sunlight;
		context->RSSetViewports(1, &finalGameRenderViewport);
		context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		context->UpdateSubresource1(m_perFrameBuffer.Get(), 0, NULL, &LightingPassBuffer, 0, 0, 0);
		context->PSSetConstantBuffers1(0, 1, m_perFrameBuffer.GetAddressOf(), nullptr, nullptr);
		// post stuff
#if ME_EDITOR
		context->OMSetRenderTargets(1, ViewRTT->RenderTargetView.GetAddressOf(), nullptr);
#else
		context->OMSetRenderTargets(1, m_device->m_d3dRenderTargetView.GetAddressOf(), nullptr);
#endif
		context->IASetInputLayout(m_lightingProgram.InputLayout.Get());
		context->VSSetShader(m_lightingProgram.VertexShader.Get(), nullptr, 0);
		context->PSSetShader(m_lightingProgram.PixelShader.Get(), nullptr, 0);
		context->PSSetShaderResources(0, 1, ViewRTT->ColorShaderResourceView.GetAddressOf());
		context->PSSetShaderResources(1, 1, ViewRTT->NormalShaderResourceView.GetAddressOf());
		context->PSSetShaderResources(2, 1, ViewRTT->SpecularShaderResourceView.GetAddressOf());
		context->PSSetShaderResources(3, 1, ViewRTT->DepthShaderResourceView.GetAddressOf());
		context->PSSetShaderResources(4, 1, ViewRTT->UIShaderResourceView.GetAddressOf());
		context->PSSetShaderResources(5, 1, ViewRTT->PositionShaderResourceView.GetAddressOf());
		context->PSSetShaderResources(6, 1, ViewRTT->ShadowMapShaderResourceView.GetAddressOf());
		context->PSSetSamplers(0, 1, m_computeSampler.GetAddressOf());
		primitiveBatch->Begin();
		VertexPositionTexCoord vert1{ {-1.f,1.f,0.f}, {0.f,0.f} };
		VertexPositionTexCoord vert2{ {-1.f,-1.f,0.f}, {0.f,1.f} };
		VertexPositionTexCoord vert3{ {1.f,1.f,0.f}, {1.f,0.f} };
		VertexPositionTexCoord vert4{ {1.f,-1.f,0.f}, {1.f,1.f} };

		VertexPositionTexCoord verts[5];
		verts[0].Position = vert1.Position;
		verts[1].Position = vert2.Position;
		verts[2].Position = vert3.Position;
		verts[3].Position = vert4.Position;
		verts[4].Position = vert2.Position;

		verts[0].TexCoord = vert1.TexCoord;
		verts[1].TexCoord = vert2.TexCoord;
		verts[2].TexCoord = vert3.TexCoord;
		verts[3].TexCoord = vert4.TexCoord;
		verts[4].TexCoord = vert2.TexCoord;

		primitiveBatch->Draw(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP, verts, 5);

		primitiveBatch->End();

		if (false)
		{
			context->UpdateSubresource1(m_perFrameBuffer.Get(), 0, NULL, &Sunlight, 0, 0, 0);
			context->PSSetConstantBuffers1(0, 1, m_perFrameBuffer.GetAddressOf(), nullptr, nullptr);
			// post stuff
			context->OMSetRenderTargets(1, ViewRTT->RenderTargetView.GetAddressOf(), nullptr);
			context->IASetInputLayout(nullptr);
			context->VSSetShader(m_dofProgram.VertexShader.Get(), nullptr, 0);
			context->PSSetShader(m_dofProgram.PixelShader.Get(), nullptr, 0);
			context->PSSetShaderResources(0, 1, ViewRTT->ShaderResourceView.GetAddressOf());
			context->PSSetShaderResources(1, 1, ViewRTT->DepthShaderResourceView.GetAddressOf());
			context->PSSetSamplers(0, 1, m_computeSampler.GetAddressOf());
			context->Draw(3, 0);
			if (ViewRTT->FinalTexture != ViewRTT->FinalTexture)
			{
				context->ResolveSubresource(ViewRTT->FinalTexture.Get(), 0, ViewRTT->FinalTexture.Get(), 0, DXGI_FORMAT_R16G16B16A16_FLOAT);
			}
		}

		if (false)//Input::GetInstance().IsKeyDown(KeyCode::A))
		{
			context->UpdateSubresource1(m_perFrameBuffer.Get(), 0, NULL, &Sunlight, 0, 0, 0);
			context->PSSetConstantBuffers1(0, 1, m_perFrameBuffer.GetAddressOf(), nullptr, nullptr);
			// post stuff
			context->OMSetRenderTargets(1, ViewRTT->RenderTargetView.GetAddressOf(), nullptr);
			context->IASetInputLayout(nullptr);
			context->VSSetShader(m_tonemapProgram.VertexShader.Get(), nullptr, 0);
			context->PSSetShader(m_tonemapProgram.PixelShader.Get(), nullptr, 0);
			context->PSSetShaderResources(0, 1, ViewRTT->ShaderResourceView.GetAddressOf());
			context->PSSetSamplers(0, 1, m_computeSampler.GetAddressOf());
			context->Draw(3, 0);
			if (ViewRTT->FinalTexture != ViewRTT->FinalTexture)
			{
				context->ResolveSubresource(ViewRTT->FinalTexture.Get(), 0, ViewRTT->FinalTexture.Get(), 0, DXGI_FORMAT_R16G16B16A16_FLOAT);
			}
		}
		ID3D11RenderTargetView* nullViews[] = { nullptr, nullptr, nullptr, nullptr };
		context->OMSetRenderTargets(ARRAYSIZE(nullViews), nullViews, nullptr);
		//context->OMSetRenderTargets(4, nullptr, nullptr);

	}

	void Renderer::DrawDepthOnlyScene(ID3D11DeviceContext3* context, DepthPassBuffer& constantBufferSceneData, FrameBuffer* ViewRTT)
	{
		Vector2 outputSize(1024,1024);
		if (outputSize.IsZero())
		{
			return;
		}
		float aspectRatio = static_cast<float>(outputSize.X()) / static_cast<float>(outputSize.Y());

		// Note that the OrientationTransform3D matrix is post-multiplied here
		// in order to correctly orient the scene to match the display orientation.
		// This post-multiplication step is required for any draw calls that are
		// made to the swap chain render target. For draw calls to other targets,
		// this transform should not be applied.


		m_device->GetD3DDeviceContext()->OMSetDepthStencilState(nullptr, 0);

			XMMATRIX orthographicMatrix = XMMatrixOrthographicRH(
				20.0f,
				20.0f,
				1.0f,
				100.0f
			);

			//XMStoreFloat4x4(
			//	&constantBufferSceneData.projection,
			//	(orthographicMatrix)
			//);
			//DirectX::SimpleMath::Matrix::CreateLookAt()
			//CreateLookAt()
		const XMVECTORF32 eye = { Sunlight.pos.x, Sunlight.pos.y, Sunlight.pos.z, 0 };
		const XMVECTORF32 at = { Sunlight.dir.x, Sunlight.dir.y, Sunlight.dir.z, 0.0f };

		const XMVECTOR up = DirectX::XMVector3Cross(DirectX::XMVector3Cross({ Sunlight.dir.x, Sunlight.dir.y, Sunlight.dir.z }, { 0.0f, 1.0f, 0.0f }), { Sunlight.dir.x, Sunlight.dir.y, Sunlight.dir.z });
		//const XMVECTORF32 up = { camera.Up.X(), camera.Up.Y(), camera.Up.Z(), 0 };

		ID3D11RenderTargetView** targets[1] = { ViewRTT->ShadowMapRenderTargetView.GetAddressOf() };
		context->OMSetRenderTargets(1, *targets, nullptr);

		CD3D11_VIEWPORT screenViewport = CD3D11_VIEWPORT(
			0.0f,
			0.0f,
			1024,
			1024
		);

		LightingPassBuffer.LightSpaceMatrix = orthographicMatrix * XMMatrixTranspose(XMMatrixLookToRH(eye, at, up));

		context->RSSetViewports(1, &screenViewport);

		//XMStoreFloat4x4(&m_constantBufferData.view, XMMatrixTranspose(XMMatrixIdentity()));
		XMStoreFloat4x4(&constantBufferSceneData.cameraMatrix, LightingPassBuffer.LightSpaceMatrix);

		//m_device->ResetCullingMode();

		if (Lights.size() > 0)
		{
			//constantBufferSceneData.light = Lights[0];
		}

		m_device->GetD3DDeviceContext()->IASetInputLayout(m_depthProgram.InputLayout.Get());

		// Attach our vertex shader.
		m_device->GetD3DDeviceContext()->VSSetShader(
			m_depthProgram.VertexShader.Get(),
			nullptr,
			0
		);
		// Attach our pixel shader.
		m_device->GetD3DDeviceContext()->PSSetShader(
			m_depthProgram.PixelShader.Get(),
			nullptr,
			0
		);

		{
			std::vector<MeshCommand> transparentMeshes;
			for (const MeshCommand& mesh : Meshes)
			{
				if (mesh.MeshMaterial && mesh.MeshMaterial->IsTransparent())
				{
					transparentMeshes.push_back(mesh);
					continue;
				}

				if (mesh.SingleMesh && mesh.MeshMaterial)
				{
					OPTICK_EVENT("Render::ModelCommand", Optick::Category::Rendering);
					XMStoreFloat4x4(&constantBufferSceneData.model, XMMatrixTranspose(mesh.Transform));

					// Prepare the constant buffer to send it to the graphics device.
					context->UpdateSubresource1(
						m_depthPassBuffer.Get(),
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
						m_depthPassBuffer.GetAddressOf(),
						nullptr,
						nullptr
					);
					context->PSSetConstantBuffers1(
						0,
						1,
						m_depthPassBuffer.GetAddressOf(),
						nullptr,
						nullptr
					);
					OPTICK_EVENT("Render::SingleMesh");

					mesh.SingleMesh->Draw(mesh.MeshMaterial, true);

					//shape->Draw(XMMatrixTranspose(XMMatrixIdentity()), DirectX::XMLoadFloat4x4(&constantBufferSceneData.view), DirectX::XMLoadFloat4x4(&constantBufferSceneData.projection), Colors::Gray, nullptr, true);
				}
			}
			m_device->ResetCullingMode();
		}
		//m_device->GetD3DDeviceContext()->OMSetBlendState(0, 0, 0xffffffff);
	}

	void Renderer::DrawPickingTexture(ID3D11DeviceContext3* context, PickingConstantBuffer& constantBufferSceneData, const CameraData& camera, FrameBuffer* ViewRTT)
	{
		Vector2 outputSize = camera.OutputSize;
		if (outputSize.IsZero())
		{
			return;
		}
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

		//m_device->GetD3DDeviceContext()->OMSetDepthStencilState(m_device->DepthStencilState, 0);
		XMMATRIX projectionMatrix;
		if (camera.Projection == ProjectionType::Perspective)
		{
			// This sample makes use of a right-handed coordinate system using row-major matrices.
			projectionMatrix = XMMatrixPerspectiveFovRH(
				fovAngleY,
				aspectRatio,
				.1f,
				1000.0f
			);

			XMStoreFloat4x4(
				&constantBufferSceneData.projection,
				XMMatrixTranspose(projectionMatrix)
			);
		}
		else if (camera.Projection == ProjectionType::Orthographic)
		{
			projectionMatrix = XMMatrixOrthographicRH(
				outputSize.X() / camera.OrthographicSize,
				outputSize.Y() / camera.OrthographicSize,
				.1f,
				100.0f
			);

			XMStoreFloat4x4(
				&constantBufferSceneData.projection,
				(projectionMatrix)
			);
		}

		const XMVECTORF32 eye = { camera.Position.X(), camera.Position.Y(), camera.Position.Z(), 0 };
		const XMVECTORF32 at = { camera.Position.X() + camera.Front.X(), camera.Position.Y() + camera.Front.Y(), camera.Position.Z() + camera.Front.Z(), 0.0f };
		const XMVECTORF32 up = { camera.Up.X(), camera.Up.Y(), camera.Up.Z(), 0 };

		Sunlight.cameraPos = { camera.Position.X(), camera.Position.Y(), camera.Position.Z(), 0 };

		ID3D11RenderTargetView** targets[1] = { ViewRTT->PickingTargetView.GetAddressOf() };
		context->OMSetRenderTargets(1, *targets, ViewRTT->DepthStencilView.Get());

		CD3D11_VIEWPORT screenViewport = CD3D11_VIEWPORT(
			0.0f,
			0.0f,
			camera.OutputSize.X(),
			camera.OutputSize.Y()
		);

		context->RSSetViewports(1, &screenViewport);

		m_device->ResetCullingMode();

		//XMStoreFloat4x4(&constantBufferSceneData.view, XMMatrixTranspose(XMMatrixLookAtRH(eye, at, up)));
		//XMStoreFloat4x4(&m_constantBufferData.view, XMMatrixTranspose(XMMatrixIdentity()));
		XMStoreFloat4x4(&constantBufferSceneData.view, XMMatrixTranspose(XMMatrixLookAtRH(eye, at, up)));

		//m_device->ResetCullingMode();

		if (Lights.size() > 0)
		{
			//constantBufferSceneData.light = Lights[0];
		}

		m_device->GetD3DDeviceContext()->IASetInputLayout(m_pickingShader.InputLayout.Get());

		// Attach our vertex shader.
		m_device->GetD3DDeviceContext()->VSSetShader(
			m_pickingShader.VertexShader.Get(),
			nullptr,
			0
		);
		// Attach our pixel shader.
		m_device->GetD3DDeviceContext()->PSSetShader(
			m_pickingShader.PixelShader.Get(),
			nullptr,
			0
		);

		{
			std::vector<MeshCommand> transparentMeshes;
			for (int i = 0; i < Meshes.size(); ++i)
			{
				const MeshCommand& mesh = Meshes[i];
				if (mesh.SingleMesh && mesh.MeshMaterial)
				{
					OPTICK_EVENT("Render::ModelCommand", Optick::Category::Rendering);
					XMStoreFloat4x4(&constantBufferSceneData.model, mesh.Transform);
					XMStoreFloat(&constantBufferSceneData.id, { (FLOAT)i });

					// Prepare the constant buffer to send it to the graphics device.
					context->UpdateSubresource1(
						m_pickingBuffer.Get(),
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
						m_pickingBuffer.GetAddressOf(),
						nullptr,
						nullptr
					);
					context->PSSetConstantBuffers1(
						0,
						1,
						m_pickingBuffer.GetAddressOf(),
						nullptr,
						nullptr
					);
					OPTICK_EVENT("Render::SingleMesh");

					mesh.SingleMesh->Draw(mesh.MeshMaterial, true);

					//shape->Draw(XMMatrixTranspose(XMMatrixIdentity()), DirectX::XMLoadFloat4x4(&constantBufferSceneData.view), DirectX::XMLoadFloat4x4(&constantBufferSceneData.projection), Colors::Gray, nullptr, true);
				}
			}
			m_device->ResetCullingMode();
		}
		//m_device->GetD3DDeviceContext()->OMSetBlendState(0, 0, 0xffffffff);
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
			static_cast<DX11Device*>(m_device)->GetD3DDevice()->CreateBuffer(
				&constantBufferDesc,
				nullptr,
				&m_constantBuffer
			)
		);

		CD3D11_BUFFER_DESC depthBufferDesc(sizeof(DepthPassBuffer), D3D11_BIND_CONSTANT_BUFFER);
		DX::ThrowIfFailed(
			static_cast<DX11Device*>(m_device)->GetD3DDevice()->CreateBuffer(
				&depthBufferDesc,
				nullptr,
				&m_depthPassBuffer
			)
		);

		CD3D11_BUFFER_DESC depthBufferDesc2(sizeof(PickingConstantBuffer), D3D11_BIND_CONSTANT_BUFFER);
		DX::ThrowIfFailed(
			static_cast<DX11Device*>(m_device)->GetD3DDevice()->CreateBuffer(
				&depthBufferDesc2,
				nullptr,
				&m_pickingBuffer
			)
		);


		CD3D11_BUFFER_DESC perFrameBufferDesc(sizeof(LightingPassConstantBuffer), D3D11_BIND_CONSTANT_BUFFER);
		/*CD3D11_BUFFER_DESC perFrameBufferDesc;
		ZeroMemory(&perFrameBufferDesc, sizeof(D3D11_BUFFER_DESC));

		perFrameBufferDesc.Usage = D3D11_USAGE_DEFAULT;
		perFrameBufferDesc.ByteWidth = sizeof(LightCommand);
		perFrameBufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		perFrameBufferDesc.CPUAccessFlags = 0;
		perFrameBufferDesc.MiscFlags = 0;*/
		DX::ThrowIfFailed(
			static_cast<DX11Device*>(m_device)->GetD3DDevice()->CreateBuffer(&perFrameBufferDesc, nullptr, &m_perFrameBuffer)
		);
		//hr = d3d11Device->CreateBuffer(&cbbd, NULL, &cbPerFrameBuffer);
	}

	unsigned int Renderer::PushDebugCollider(const DebugColliderCommand& NewModel)
	{
		if (!FreeDebugColliderCommandIndicies.empty())
		{
			unsigned int openIndex = FreeDebugColliderCommandIndicies.front();
			FreeDebugColliderCommandIndicies.pop();
			DebugColliders[openIndex] = std::move(NewModel);
			return openIndex;
		}

		DebugColliders.push_back(std::move(NewModel));
		return static_cast<unsigned int>(DebugColliders.size() - 1);
	}

	bool Renderer::PopDebugCollider(unsigned int id)
	{
		if (id >= DebugColliders.size())
		{
			return false;
		}

		DebugColliders[id].Transform = XMMatrixIdentity();
		return true;
	}

	void Renderer::ClearDebugColliders()
	{
		DebugColliders.clear();
		while (!FreeDebugColliderCommandIndicies.empty())
		{
			FreeDebugColliderCommandIndicies.pop();
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
		ResizeBuffers();
	}

	void Renderer::PickScene(const Vector2& Pos)
	{
		m_pickingRequested = true;
		pickingLocation = Pos;
		return;
		if (SceneViewRTT)
		{
//			SaveTextureToBmp(StringUtils::ToWString(Path("Assets/EXPORT.bmp").FullPath).c_str(), SceneViewRTT->PickingTexture.Get(), Pos);
		}
	}

	unsigned int Renderer::PushMesh(Moonlight::MeshCommand command)
	{
		switch (command.Type)
		{
		case MeshType::Plane:
			command.SingleMesh = PlaneMesh;
			break;
		case MeshType::Cube:
			break;
		case MeshType::Model:
		default:
			break;

		}
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

	void Renderer::PopMesh(unsigned int Id)
	{
		if (Id > Meshes.size())
		{
			return;
		}

		FreeMeshCommandIndicies.push(Id);
		Meshes[Id] = MeshCommand();
	}

	void Renderer::ClearMeshes()
	{
		Meshes.clear();
		while (!FreeMeshCommandIndicies.empty())
		{
			FreeMeshCommandIndicies.pop();
		}
	}

	unsigned int Renderer::PushCamera(Moonlight::CameraData& command)
	{
		unsigned int index = 0;
		if (!FreeCameraCommandIndicies.empty())
		{
			index = FreeCameraCommandIndicies.front();
			FreeCameraCommandIndicies.pop();
			Cameras[index] = std::move(command);
		}
		else
		{
			Cameras.push_back(std::move(command));
			index = static_cast<unsigned int>(Cameras.size() - 1);
		}

		CameraData& data = Cameras[index];

		delete data.Buffer;
		if (data.IsMain)
		{
			GameViewRTT = data.Buffer = m_device->CreateFrameBuffer(m_device->GetOutputSize().X(), m_device->GetOutputSize().Y(), 1, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_D24_UNORM_S8_UINT);
		}
		else
		{
			data.Buffer = m_device->CreateFrameBuffer(m_device->GetOutputSize().X(), m_device->GetOutputSize().Y(), 1, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_D24_UNORM_S8_UINT);
		}

		return index;
	}

	void Renderer::PopCamera(unsigned int Id)
	{
		if (Id > Cameras.size())
		{
			return;
		}

		if (Cameras[Id].Buffer == GameViewRTT)
		{
			GameViewRTT = nullptr;
		}
		delete Cameras[Id].Buffer;

		FreeCameraCommandIndicies.push(Id);
		Cameras[Id] = CameraData();
	}

	void Renderer::ClearUIText()
	{
		Cameras.clear();
		while (!FreeCameraCommandIndicies.empty())
		{
			FreeCameraCommandIndicies.pop();
		}
	}

	void Renderer::SaveTextureToBmp(PCWSTR FileName, ID3D11Texture2D* Texture, const CameraData& camera, const Vector2& Pos)
	{
		HRESULT hr;

		// First verify that we can map the texture
		D3D11_TEXTURE2D_DESC desc;
		Texture->GetDesc(&desc);

		// translate texture format to WIC format. We support only BGRA and ARGB.
		GUID wicFormatGuid;
		switch (desc.Format) {
		case DXGI_FORMAT_R8G8B8A8_UNORM:
			wicFormatGuid = GUID_WICPixelFormat32bppRGBA;
			break;
		case DXGI_FORMAT_B8G8R8A8_UNORM:
			wicFormatGuid = GUID_WICPixelFormat32bppBGRA;
			break;
		default:
			YIKES("Unsupported DXGI_FORMAT: %d. Only RGBA and BGRA are supported.");
		}

		// Get the device context
		Microsoft::WRL::ComPtr<ID3D11Device> d3dDevice;
		Texture->GetDevice(&d3dDevice);
		Microsoft::WRL::ComPtr<ID3D11DeviceContext> d3dContext;
		d3dDevice->GetImmediateContext(&d3dContext);

		// map the texture
		Microsoft::WRL::ComPtr<ID3D11Texture2D> mappedTexture;
		D3D11_MAPPED_SUBRESOURCE mapInfo;
		mapInfo.RowPitch;
		hr = d3dContext->Map(
			Texture,
			0,  // Subresource
			D3D11_MAP_READ,
			0,  // MapFlags
			&mapInfo);

		if (FAILED(hr)) {
			// If we failed to map the texture, copy it to a staging resource
			if (hr == E_INVALIDARG) {
				D3D11_TEXTURE2D_DESC desc2;
				desc2.Width = desc.Width;
				desc2.Height = desc.Height;
				desc2.MipLevels = desc.MipLevels;
				desc2.ArraySize = desc.ArraySize;
				desc2.Format = desc.Format;
				desc2.SampleDesc = desc.SampleDesc;
				desc2.Usage = D3D11_USAGE_STAGING;
				desc2.BindFlags = 0;
				desc2.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
				desc2.MiscFlags = 0;

				Microsoft::WRL::ComPtr<ID3D11Texture2D> stagingTexture;
				hr = d3dDevice->CreateTexture2D(&desc2, nullptr, &stagingTexture);
				if (FAILED(hr)) {
					//throw MyException::Make(hr, L"Failed to create staging texture");
				}

				// copy the texture to a staging resource
				d3dContext->CopyResource(stagingTexture.Get(), Texture);

				// now, map the staging resource
				hr = d3dContext->Map(
					stagingTexture.Get(),
					0,
					D3D11_MAP_READ,
					0,
					&mapInfo);
				if (FAILED(hr)) {
					//throw MyException::Make(hr, L"Failed to map staging texture");
				}

				mappedTexture = std::move(stagingTexture);
			}
			else {
				//throw MyException::Make(hr, L"Failed to map texture.");
			}
		}
		else {
			mappedTexture = Texture;
		}
		auto unmapResource = [&] {
			d3dContext->Unmap(mappedTexture.Get(), 0);
		};

		if (false)
		{
			Microsoft::WRL::ComPtr<IWICImagingFactory> wicFactory;
			hr = CoCreateInstance(
				CLSID_WICImagingFactory,
				nullptr,
				CLSCTX_INPROC_SERVER,
				__uuidof(wicFactory),
				reinterpret_cast<void**>(wicFactory.GetAddressOf()));
			if (FAILED(hr)) {
				YIKES("Failed to create instance of WICImagingFactory");
			}

			Microsoft::WRL::ComPtr<IWICBitmapEncoder> wicEncoder;
			hr = wicFactory->CreateEncoder(
				GUID_ContainerFormatBmp,
				nullptr,
				&wicEncoder);
			if (FAILED(hr)) {
				YIKES("Failed to create BMP encoder");
			}

			Microsoft::WRL::ComPtr<IWICStream> wicStream;
			hr = wicFactory->CreateStream(&wicStream);
			if (FAILED(hr)) {
				YIKES("Failed to create IWICStream");
			}

			hr = wicStream->InitializeFromFilename(FileName, GENERIC_WRITE);
			if (FAILED(hr)) {
				YIKES("Failed to initialize stream from file name");
			}

			hr = wicEncoder->Initialize(wicStream.Get(), WICBitmapEncoderNoCache);
			if (FAILED(hr)) {
				YIKES("Failed to initialize bitmap encoder");
			}

			// Encode and commit the frame
			{
				Microsoft::WRL::ComPtr<IWICBitmapFrameEncode> frameEncode;
				wicEncoder->CreateNewFrame(&frameEncode, nullptr);
				if (FAILED(hr)) {
					YIKES("Failed to create IWICBitmapFrameEncode");
				}

				hr = frameEncode->Initialize(nullptr);
				if (FAILED(hr)) {
					YIKES("Failed to initialize IWICBitmapFrameEncode");
				}


				hr = frameEncode->SetPixelFormat(&wicFormatGuid);
				if (FAILED(hr)) {
					YIKES("SetPixelFormat(%s) failed.");
				}

				hr = frameEncode->SetSize(desc.Width, desc.Height);
				if (FAILED(hr)) {
					YIKES("SetSize(...) failed.");
				}

				hr = frameEncode->WritePixels(
					desc.Height,
					mapInfo.RowPitch,
					desc.Height * mapInfo.RowPitch,
					reinterpret_cast<BYTE*>(mapInfo.pData));
				if (FAILED(hr)) {
					YIKES("frameEncode->WritePixels(...) failed.");
				}


				hr = frameEncode->Commit();
				if (FAILED(hr)) {
					YIKES("Failed to commit frameEncode");
				}
				//std::unique_ptr<BYTE> pBuf(new BYTE[mapInfo.RowPitch * desc.Height]);
			}

			hr = wicEncoder->Commit();
			if (FAILED(hr)) {
				YIKES("Failed to commit encoder");
			}
		}

		{
			BYTE* g_iMageBuffer = reinterpret_cast<BYTE*>(mapInfo.pData);

			int x = Pos.X();
			int y = Pos.Y();
			if (x < 0.0f || y < 0.0f || x > camera.OutputSize.X() || y > camera.OutputSize.Y())
			{
				return;
			}
			x = Mathf::Clamp(0.f, camera.OutputSize.X() /*desc.Width / 4*/, x);
			y = Mathf::Clamp(0.f, camera.OutputSize.Y(), y);

			int bonusX = desc.Width - camera.OutputSize.X();
			int bufSize = 4 * (x + y * desc.Width);
			BYTE R = g_iMageBuffer[bufSize];
			//BYTE G = g_iMageBuffer[1 + 4 * (x + y * desc.Width)];
			//BYTE B = g_iMageBuffer[2 + 4 * (x + y * desc.Width)];
			BYTE A = g_iMageBuffer[3 + 4 * (x + y * desc.Width)];

			if ((unsigned int)A > 0)
			{
				PickingEvent evt;
				evt.Id = (unsigned int)R;
				evt.Fire();
			}
		}
	}
}
