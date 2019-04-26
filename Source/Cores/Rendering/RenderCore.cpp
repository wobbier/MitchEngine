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

#include "Components/Camera.h"
#include <iostream>
#include "Components/Physics/Rigidbody.h"
#include "Resource/ResourceCache.h"
#include "Graphics/ModelResource.h"
#include "RenderCommands.h"

RenderCore::RenderCore()
	: Base(ComponentFilter().Requires<Transform>().RequiresOneOf<Model>().RequiresOneOf<Rigidbody>())
{
	//m_sceneRenderer = std::unique_ptr<TestModelRenderer>(new TestModelRenderer(m_deviceResources));
	m_renderer = &Game::GetEngine().GetRenderer();
	m_renderer->RegisterDeviceNotify(this);

	cube = ResourceCache::GetInstance().Get<ModelResource>(FilePath("Assets/cube.fbx"));
	shader = new Moonlight::Shader("Assets/Shaders/SimpleVertexShader.cso", "Assets/Shaders/SimplePixelShader.cso");
}

void RenderCore::Init()
{
	Logger::GetInstance().Log(Logger::LogType::Debug, "RenderCore Initialized...");
	m_renderer->ClearModels();
}

void RenderCore::OnEntityAdded(Entity& NewEntity)
{
	if (NewEntity.HasComponent<Model>())
	{
		Moonlight::ModelCommand command;
		Model& model = NewEntity.GetComponent<Model>();
		model.Init();
		command.Meshes = model.ModelHandle->Meshes;
		command.ModelShader = model.ModelShader;
		model.Id = Game::GetEngine().GetRenderer().PushModel(command);
	}

	if (NewEntity.HasComponent<Rigidbody>())
	{
		Moonlight::ModelCommand command;
		Rigidbody& rigidbody = NewEntity.GetComponent<Rigidbody>();

		command.Meshes = cube->Meshes;
		command.ModelShader = shader;//model.ModelShader;

		rigidbody.Id = Game::GetEngine().GetRenderer().PushModel(command);
		//rigidbody.GetColliderType();
	}
}

RenderCore::~RenderCore()
{
	Logger::GetInstance().Log(Logger::LogType::Debug, "RenderCore Destroyed...");
}

void RenderCore::Update(float dt)
{
	m_renderer->Update(dt);
	OPTICK_CATEGORY("RenderCore::Update", Optick::Category::Rendering)

	auto Renderables = GetEntities();
	for (auto& InEntity : Renderables)
	{
		Transform& transform = InEntity.GetComponent<Transform>();
		if (InEntity.HasComponent<Model>())
		{
			Model& model = InEntity.GetComponent<Model>();
			m_renderer->UpdateMatrix(model.GetId(), transform.GetMatrix());
		}

		if (InEntity.HasComponent<Rigidbody>())
		{
			Rigidbody& rigidbody = InEntity.GetComponent<Rigidbody>();
			m_renderer->UpdateMatrix(rigidbody.Id, rigidbody.GetMat());
		}
	}
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