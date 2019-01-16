#include "PCH.h"
#include "RenderCore.h"
#include "Components/Debug/DebugCube.h"
#include "Components/Debug/DebugCube.h"
#include "Components/Graphics/Model.h"
#include "Components/Transform.h"
#include "ECS/ComponentFilter.h"
#include "Logger.h"
#include "Graphics/Shader.h"
#include "Resource/Resource.h"

#include <glm.hpp>
#include <gtc/matrix_transform.hpp>
#include <gtc/type_ptr.hpp>
#include <gtx/rotate_vector.hpp>
#include <gtx/quaternion.hpp>

#include "Components/Camera.h"
#include <iostream>

RenderCore::RenderCore()
	: Base(ComponentFilter().Requires<Transform>().Requires<Model>())
{
	//m_sceneRenderer = std::unique_ptr<TestModelRenderer>(new TestModelRenderer(m_deviceResources));
	m_renderer = &Game::GetEngine().GetRenderer();
	m_renderer->RegisterDeviceNotify(this);
}

void RenderCore::Init()
{
	Logger::GetInstance().Log(Logger::LogType::Debug, "RenderCore Initialized...");
}

void RenderCore::Update(float dt)
{
	m_renderer->Update(dt);
}

RenderCore::~RenderCore()
{
	Logger::GetInstance().Log(Logger::LogType::Debug, "RenderCore Destroyed...");
}

bool RenderCore::Render()
{
	BROFILER_CATEGORY("Renderer::Render", Brofiler::Color::Red)
	Camera* CurrentCamera = Camera::CurrentCamera;
	if (!CurrentCamera)
	{
		return false;
	}

	auto Renderables = GetEntities();
	for (auto& InEntity : Renderables)
	{
	}

	m_renderer->Render();

	return true;
}

void RenderCore::OnDeviceLost()
{
#if ME_DIRECTX
	m_renderer->ReleaseDeviceDependentResources();
#endif
}

void RenderCore::OnDeviceRestored()
{
#if ME_DIRECTX
	m_renderer->CreateDeviceDependentResources();
	m_renderer->GetDevice().CreateWindowSizeDependentResources();
#endif
}