#include "PCH.h"
#include "RenderCore.h"
#include "Components/Debug/DebugCube.h"
#include "Components/Debug/DebugCube.h"
#include "Components/Graphics/Model.h"
#include "Components/Transform.h"
#include "ECS/ComponentFilter.h"
#include "Logger.h"
#include "Graphics/ShaderCommand.h"
#include "Resource/Resource.h"

#include "Components/Camera.h"
#include <iostream>
#include "Components/Physics/Rigidbody.h"
#include "Resource/ResourceCache.h"
#include "Graphics/ModelResource.h"
#include "RenderCommands.h"
#include "Components/Lighting/Light.h"
#include "Components/Graphics/Mesh.h"
#include "Engine/Engine.h"
#include "Components/Lighting/DirectionalLight.h"

RenderCore::RenderCore()
	: Base(ComponentFilter().Requires<Transform>().RequiresOneOf<Model>().RequiresOneOf<Rigidbody>().RequiresOneOf<Light>().RequiresOneOf<Mesh>().RequiresOneOf<DirectionalLight>())
{
	IsSerializable = false;
	m_renderer = &GetEngine().GetRenderer();
	m_renderer->RegisterDeviceNotify(this);
}

void RenderCore::Init()
{
	Logger::GetInstance().Log(Logger::LogType::Debug, "RenderCore Initialized...");
	m_renderer->ClearDebugColliders();
	m_renderer->ClearMeshes();
}

void RenderCore::OnEntityAdded(Entity& NewEntity)
{
	if (NewEntity.HasComponent<Mesh>())
	{
		Moonlight::MeshCommand command;
		Mesh& model = NewEntity.GetComponent<Mesh>();
		command.SingleMesh = model.MeshReferece;
		command.MeshShader = model.MeshShader;
		command.MeshMaterial = model.MeshMaterial;
		model.Id = GetEngine().GetRenderer().PushMesh(command);
	}
	if (NewEntity.HasComponent<DirectionalLight>())
	{
		DirectionalLight& light = NewEntity.GetComponent<DirectionalLight>();
		GetEngine().GetRenderer().Sunlight.ambient = light.Ambient;
		GetEngine().GetRenderer().Sunlight.diffuse = light.Diffuse;
		GetEngine().GetRenderer().Sunlight.dir = light.Direction;
	}
}

void RenderCore::OnEntityRemoved(Entity& InEntity)
{
	if (InEntity.HasComponent<Mesh>())
	{
		Mesh& mesh = InEntity.GetComponent<Mesh>();
		GetEngine().GetRenderer().PopMesh(mesh.Id);
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

		if (InEntity.HasComponent<Mesh>())
		{
			Mesh& model = InEntity.GetComponent<Mesh>();
			m_renderer->UpdateMeshMatrix(model.GetId(), transform.GetMatrix().GetInternalMatrix());
		}

		if (InEntity.HasComponent<Rigidbody>())
		{
			Rigidbody& rigidbody = InEntity.GetComponent<Rigidbody>();
			if (rigidbody.IsRigidbodyInitialized())
			{
				// TODO: Use the matrix from the rigidbody
				m_renderer->UpdateMatrix(rigidbody.Id, transform.GetMatrix().GetInternalMatrix());
			}
		}
		if (InEntity.HasComponent<DirectionalLight>())
		{
			DirectionalLight& light = InEntity.GetComponent<DirectionalLight>();
			m_renderer->Sunlight.ambient = light.Ambient;
			m_renderer->Sunlight.diffuse = light.Diffuse;
			m_renderer->Sunlight.dir = light.Direction;
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

void RenderCore::OnStop()
{
	m_renderer->ClearMeshes();
	m_renderer->ClearDebugColliders();
}
