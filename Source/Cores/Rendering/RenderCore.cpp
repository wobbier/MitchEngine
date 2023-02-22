#include "PCH.h"
#include "RenderCore.h"
#include "Components/Debug/DebugCube.h"
#include "Components/Debug/DebugCube.h"
#include "Components/Graphics/Model.h"
#include "Components/Transform.h"
#include "ECS/ComponentFilter.h"
#include "CLog.h"
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
#include "Work/Burst.h"
#include "Renderer.h"
#include <Core/JobSystem.h>

RenderCore::RenderCore()
	: Base(ComponentFilter().Requires<Transform>().Requires<Mesh>())
{
	SetIsSerializable(false);
	//m_renderer = &GetEngine().GetRenderer();
	//m_renderer->RegisterDeviceNotify(this);
}

void RenderCore::Init()
{
	CLog::GetInstance().Log(CLog::LogType::Debug, "RenderCore Initialized...");
	//m_renderer->ClearDebugColliders();
	GetEngine().GetRenderer().ClearMeshes();
}

void RenderCore::OnEntityAdded(Entity& NewEntity)
{
	if (NewEntity.HasComponent<Mesh>())
	{
		Moonlight::MeshCommand command;
		Mesh& model = NewEntity.GetComponent<Mesh>();
		command.SingleMesh = model.MeshReferece;
		command.MeshMaterial = model.MeshMaterial;
		command.Transform = NewEntity.GetComponent<Transform>().GetMatrix().GetInternalMatrix();
		command.Type = model.GetType();
		model.Id = GetEngine().GetRenderer().GetMeshCache().Push(command);
	}
	if (NewEntity.HasComponent<DirectionalLight>())
	{
		//DirectionalLight& light = NewEntity.GetComponent<DirectionalLight>();
		//GetEngine().GetRenderer().Sunlight.ambient = light.Ambient;
		//GetEngine().GetRenderer().Sunlight.diffuse = light.Diffuse;
		//GetEngine().GetRenderer().Sunlight.dir = light.Direction;
	}
}

void RenderCore::OnEntityRemoved(Entity& InEntity)
{
	if (InEntity.HasComponent<Mesh>())
	{
		Mesh& mesh = InEntity.GetComponent<Mesh>();
		GetEngine().GetRenderer().GetMeshCache().Pop(mesh.Id);
	}
}

RenderCore::~RenderCore()
{
	CLog::GetInstance().Log(CLog::LogType::Debug, "RenderCore Destroyed...");
}

void RenderCore::Update(const UpdateContext& inUpdateContext)
{
	OPTICK_CATEGORY("RenderCore::Update", Optick::Category::Rendering)
	//m_renderer->Update(dt);

	auto& Renderables = GetEntities();
	if (Renderables.empty())
	{
		return;
	}
    
    //auto [worker, pool] = GetEngine().GetJobSystemNew();

    Worker* worker = GetEngine().GetJobEngine().GetThreadWorker();
    Job* rootJob = worker->GetPool().CreateClosureJob([&Renderables, worker](Job& job) {
    
    });

	std::vector<std::pair<int, int>> batches;
	Burst::GenerateChunks(Renderables.size(), 11, batches);

	worker->Submit(rootJob);
	for (auto& batch : batches)
	{
		int batchBegin = batch.first;
		int batchEnd = batch.second;
		int batchSize = batchEnd - batchBegin;

		//YIKES(std::to_string(batchBegin) + " End:" + std::to_string(batchEnd) + " Size:" + std::to_string(batchSize));
		Job* burstJob = worker->GetPool().CreateClosureJobAsChild([&Renderables, batchBegin, batchEnd, batchSize](Job& job) {
			if (Renderables.size() == 0)
			{
				return;
			}
			for (int entIndex = batchBegin; entIndex < batchEnd; ++entIndex)
			{
				auto& InEntity = Renderables[entIndex];
				OPTICK_CATEGORY("B::Update Mesh Matrix", Optick::Category::Debug);

				if (InEntity/* && !InEntity.IsLoading*/)
				{

					Transform& transform = InEntity.GetComponent<Transform>();
					if (InEntity.HasComponent<Mesh>())
					{
						Mesh& model = InEntity.GetComponent<Mesh>();

						GetEngine().GetRenderer().UpdateMeshMatrix(model.GetId(), transform.GetMatrix().GetInternalMatrix());
					}
					if (InEntity.HasComponent<DirectionalLight>())
					{
						//DirectionalLight& light = InEntity.GetComponent<DirectionalLight>();
						//auto pos = transform.GetWorldPosition();
						//m_renderer->Sunlight.pos = XMFLOAT4(pos.x, pos.y, pos.z, 0);
						//m_renderer->Sunlight.ambient = light.Ambient;
						//m_renderer->Sunlight.diffuse = light.Diffuse;
						//m_renderer->Sunlight.dir = light.Direction;
					}
				}

			}
		}, rootJob);
		worker->Submit(burstJob);
	}
    worker->Wait(rootJob);
	//for (auto& InEntity : Renderables)
	//{
	//	//OPTICK_CATEGORY("Update Mesh", Optick::Category::Rendering);

	//	Burst::LambdaWorkEntry entry;
	//	entry.m_callBack = [this, &InEntity]() {
	//		OPTICK_CATEGORY("Burst::TestFunc", Optick::Category::Debug);
	//		Transform& transform = InEntity.GetComponent<Transform>();
	//		Mesh& model = InEntity.GetComponent<Mesh>();

	//		m_renderer->UpdateMeshMatrix(model.GetId(), transform.GetMatrix().GetInternalMatrix());
	//	};
	//	burst.AddWork2<Burst::LambdaWorkEntry>(entry, (int)sizeof(Burst::LambdaWorkEntry));
	//	//if (InEntity.HasComponent<Rigidbody>())
	//	//{
	//	//	Rigidbody& rigidbody = InEntity.GetComponent<Rigidbody>();
	//	//	if (rigidbody.IsRigidbodyInitialized())
	//	//	{
	//	//		// TODO: Use the matrix from the rigidbody
	//	//		m_renderer->UpdateMatrix(rigidbody.Id, transform.GetMatrix().GetInternalMatrix());
	//	//	}
	//	//}
	//	//if (InEntity.HasComponent<DirectionalLight>())
	//	//{
	//	//	DirectionalLight& light = InEntity.GetComponent<DirectionalLight>();
	//	//	auto pos = transform.GetWorldPosition().GetInternalVec();
	//	//	m_renderer->Sunlight.pos = XMFLOAT4(pos.x, pos.y, pos.z, 0);
	//	//	m_renderer->Sunlight.ambient = light.Ambient;
	//	//	m_renderer->Sunlight.diffuse = light.Diffuse;
	//	//	m_renderer->Sunlight.dir = light.Direction;
	//	//}
	//}
#if ME_EDITOR
	GetEngine().GetRenderer().SetDebugDrawEnabled(EnableDebugDraw);
#endif
}

void RenderCore::OnDeviceLost()
{
#if ME_DIRECTX
	//m_renderer->ReleaseDeviceDependentResources();
#endif
}

void RenderCore::OnDeviceRestored()
{
#if ME_DIRECTX
	//m_renderer->CreateDeviceDependentResources();
	//m_renderer->GetDevice().CreateWindowSizeDependentResources();
#endif
}

void RenderCore::OnStop()
{
	GetEngine().GetRenderer().ClearMeshes();
	//m_renderer->ClearDebugColliders();
}

void RenderCore::UpdateMesh(Mesh* InMesh)
{
	GetEngine().GetRenderer().GetMeshCache().Pop(InMesh->Id);

	Moonlight::MeshCommand command;
	command.SingleMesh = InMesh->MeshReferece;
	command.MeshMaterial = InMesh->MeshMaterial;
	command.Type = InMesh->GetType();
	InMesh->Id = GetEngine().GetRenderer().GetMeshCache().Push(command);
}

#if ME_EDITOR

void RenderCore::OnEditorInspect()
{
	Base::OnEditorInspect();

	ImGui::Checkbox("Enable Debug Draw", &EnableDebugDraw);
	if (ImGui::Button("MSAA None"))
	{
		GetEngine().GetRenderer().SetMSAALevel(BGFXRenderer::MSAALevel::None);
	}
	if (ImGui::Button("MSAA X16"))
	{
		GetEngine().GetRenderer().SetMSAALevel(BGFXRenderer::MSAALevel::X16);
	}


}

#endif