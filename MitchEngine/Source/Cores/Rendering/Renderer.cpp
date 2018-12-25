#include "PCH.h"
#include "Renderer.h"
#include "Components/Debug/DebugCube.h"
#include "Components/Debug/DebugCube.h"
#include "Components/Graphics/Model.h"
#include "Components/Sprite.h"
#include "Components/Transform.h"
#include "ECS/ComponentFilter.h"
#include "Utility/Logger.h"
#include "Engine/Window.h"
#include "Graphics/Shader.h"
#include "Engine/Resource.h"

#include <glm.hpp>
#include <gtc/matrix_transform.hpp>
#include <gtc/type_ptr.hpp>
#include <gtx/rotate_vector.hpp>
#include <gtx/quaternion.hpp>

#include "Components/Camera.h"
#include <iostream>

#ifdef ME_PLATFORM_UWP
Renderer::Renderer(const std::shared_ptr<DX::DeviceResources>& deviceResources)
	: Base(ComponentFilter().Requires<Transform>().Requires<Model>())
	, m_deviceResources(deviceResources)
{
	m_deviceResources->RegisterDeviceNotify(this);
	m_sceneRenderer = std::unique_ptr<TestModelRenderer>(new TestModelRenderer(m_deviceResources));

	m_fpsTextRenderer = std::unique_ptr<SampleFpsTextRenderer>(new SampleFpsTextRenderer(m_deviceResources));

}
#endif

Renderer::Renderer()
	: Base(ComponentFilter().Requires<Transform>().Requires<Model>())
{

}

void Renderer::Init()
{
	Logger::GetInstance().Log(Logger::LogType::Debug, "Renderer Initialized...");
	//Logger::GetInstance().Log(Logger::LogType::Debug, (const char*)glGetString(GL_VERSION));

	SkyboxMap = Cubemap::Load("Assets/skybox");
	SkyboxShader = new Shader("Assets/Shaders/Skybox.vert", "Assets/Shaders/Skybox.frag");
	float skyboxVertices[] = {
		// positions          
		-1.0f,  1.0f, -1.0f,
		-1.0f, -1.0f, -1.0f,
		1.0f, -1.0f, -1.0f,
		1.0f, -1.0f, -1.0f,
		1.0f,  1.0f, -1.0f,
		-1.0f,  1.0f, -1.0f,

		-1.0f, -1.0f,  1.0f,
		-1.0f, -1.0f, -1.0f,
		-1.0f,  1.0f, -1.0f,
		-1.0f,  1.0f, -1.0f,
		-1.0f,  1.0f,  1.0f,
		-1.0f, -1.0f,  1.0f,

		1.0f, -1.0f, -1.0f,
		1.0f, -1.0f,  1.0f,
		1.0f,  1.0f,  1.0f,
		1.0f,  1.0f,  1.0f,
		1.0f,  1.0f, -1.0f,
		1.0f, -1.0f, -1.0f,

		-1.0f, -1.0f,  1.0f,
		-1.0f,  1.0f,  1.0f,
		1.0f,  1.0f,  1.0f,
		1.0f,  1.0f,  1.0f,
		1.0f, -1.0f,  1.0f,
		-1.0f, -1.0f,  1.0f,

		-1.0f,  1.0f, -1.0f,
		1.0f,  1.0f, -1.0f,
		1.0f,  1.0f,  1.0f,
		1.0f,  1.0f,  1.0f,
		-1.0f,  1.0f,  1.0f,
		-1.0f,  1.0f, -1.0f,

		-1.0f, -1.0f, -1.0f,
		-1.0f, -1.0f,  1.0f,
		1.0f, -1.0f, -1.0f,
		1.0f, -1.0f, -1.0f,
		-1.0f, -1.0f,  1.0f,
		1.0f, -1.0f,  1.0f
	};
}

void Renderer::Update(float dt)
{
#ifdef ME_PLATFORM_UWP
	m_timer.Tick([&]()
	{
		m_sceneRenderer->Update(m_timer);
		m_fpsTextRenderer->Update(m_timer);
	});
#endif
}

Renderer::~Renderer()
{
#ifdef ME_PLATFORM_UWP
	m_deviceResources->RegisterDeviceNotify(nullptr);
#endif
	Logger::GetInstance().Log(Logger::LogType::Debug, "Renderer Destroyed...");
}

bool Renderer::Render()
{
	//BROFILER_CATEGORY("Renderer::Render", Brofiler::Color::Red)
	Camera* CurrentCamera = Camera::CurrentCamera;
#ifdef ME_PLATFORM_UWP
	if (!CurrentCamera || m_timer.GetFrameCount() == 0)
	{
		return false;
	}

	auto context = m_deviceResources->GetD3DDeviceContext();

	// Reset the viewport to target the whole screen.
	auto viewport = m_deviceResources->GetScreenViewport();
	context->RSSetViewports(1, &viewport);

	// Reset render targets to the screen.
	ID3D11RenderTargetView *const targets[1] = { m_deviceResources->GetBackBufferRenderTargetView() };
	context->OMSetRenderTargets(1, targets, m_deviceResources->GetDepthStencilView());

	// Clear the back buffer and depth stencil view.
	context->ClearRenderTargetView(m_deviceResources->GetBackBufferRenderTargetView(), DirectX::Colors::CornflowerBlue);
	context->ClearDepthStencilView(m_deviceResources->GetDepthStencilView(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

	auto Renderables = GetEntities();

	for (Entity ent : Renderables)
	{
		ent.GetComponent<Model>().Draw();
	}

	// Render the scene objects.
	// TODO: Replace this with your app's content rendering functions.
	m_sceneRenderer->Render(GetEntities());
	m_fpsTextRenderer->Render();
#endif
	return true;
}

#ifdef ME_PLATFORM_UWP
void Renderer::OnDeviceLost()
{
	m_sceneRenderer->ReleaseDeviceDependentResources();
	m_fpsTextRenderer->ReleaseDeviceDependentResources();
}

void Renderer::OnDeviceRestored()
{
	m_sceneRenderer->CreateDeviceDependentResources();
	m_fpsTextRenderer->CreateDeviceDependentResources();
	CreateWindowSizeDependentResources();
}

void Renderer::CreateWindowSizeDependentResources()
{
	m_sceneRenderer->CreateWindowSizeDependentResources();
}
#endif