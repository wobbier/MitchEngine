#include "PCH.h"
#include "Cores/SceneGraph.h"
#include "Components/Transform.h"
#include "Engine/World.h"
#include <stack>
#include "Events/SceneEvents.h"
#include "Engine/Engine.h"
#include "optick.h"
#include "Work/Burst.h"
#include <Work/JobQueue.h>
#include <Core/JobQueueOld.h>

SceneGraph::SceneGraph()
	: Base(ComponentFilter().Requires<Transform>())
{
	SetIsSerializable(false);

	std::vector<TypeId> eventIds;
	eventIds.push_back(SceneLoadedEvent::GetEventId());
	EventManager::GetInstance().RegisterReceiver(this, eventIds);
}

SceneGraph::~SceneGraph()
{
}

void SceneGraph::Init()
{
	//RootTransform->Children.clear();
	if (!RootTransform)
	{
		RootTransform = GameWorld->CreateEntity();
		RootTransform->AddComponent<Transform>();
	}
}
bool shouldBurst = false;

void UpdateRecursively(Transform* CurrentTransform, bool isParentDirty, Job* parentJob)
{
	auto [worker, pool] = GetEngine().GetJobSystemNew();
	OPTICK_EVENT("SceneGraph::UpdateRecursively");
	for (const SharedPtr<Transform>& Child : CurrentTransform->GetChildren())
	{
		OPTICK_EVENT("SceneGraph::UpdateRecursively::GetChildren");
		bool newParentDirty = isParentDirty || Child->IsDirty();
		Job* job = pool.CreateClosureJobAsChild([&Child, newParentDirty, CurrentTransform](Job& job) {
			if (newParentDirty)
			{
				OPTICK_CATEGORY("Update Transform", Optick::Category::Scene);

				glm::mat4 model = glm::mat4(1.f);
				model = glm::translate(model, Child->GetPosition().InternalVector);
				model = glm::rotate(model, Child->GetWorldRotation().ToAngle(), Child->GetWorldRotation().ToAxis().InternalVector);
				model = glm::scale(model, Child->GetScale().InternalVector);

				Matrix4 xxx = model * CurrentTransform->WorldTransform.GetInternalMatrix();
				Child->SetWorldTransform(xxx);
			}
		}, parentJob);

		worker->Submit(job);

		if (!Child->GetChildren().empty())
		{
			UpdateRecursively(Child.get(), newParentDirty, job);// *job);
		}

	}
}

void recursiveJob(Job* parent, int currentDepth)
{
	int i = 0;
	//for (std::size_t i = 0; i < 5; ++i)
	{
		Worker* worker = GetEngine().GetJobEngine().GetThreadWorker();
		Job* root2 = worker->GetPool().CreateClosureJobAsChild([currentDepth, i](Job& job) {
			OPTICK_CATEGORY("Child Job", Optick::Category::Debug)
				if (currentDepth < 5)
				{
					//std::cout << std::to_string(currentDepth) << ":" << std::to_string(i) << std::endl;
					recursiveJob(&job, currentDepth + 1);
				}
		}, parent);
		worker->Submit(root2);
	}
}

void SceneGraph::Update(float dt)
{
	OPTICK_EVENT("SceneGraph::Update");

	auto [worker, pool] = GetEngine().GetJobSystemNew(); 

	for (int i = 0; i < 2; ++i)
	{
		OPTICK_FRAME("Main Loop");
		Job* root = worker->GetPool().CreateJob([](Job& job) {
			OPTICK_CATEGORY("Initial Job", Optick::Category::Debug);
			recursiveJob(&job, 0);
		});
		worker->Submit(root);
		worker->Wait(root);
	}

	if(true)
	{
		Job* job = pool.CreateClosureJob([this](Job& job) {
			UpdateRecursively(GetRootTransform(), false, &job);
		});
		worker->Submit(job);
		worker->Wait(job);
	}
    GetEngine().GetJobEngine().ClearWorkerPools();
	// Seems O.K. for now
	//GetEngine().GetJobQueue().Push([this]() {
	//	UpdateRecursively(GetRootTransform(), false, false);
	//});
	//GetEngine().GetJobSystem().Wait();
}

void SceneGraph::OnEntityAdded(Entity& NewEntity)
{
	Base::OnEntityAdded(NewEntity);

	Transform& NewEntityTransform = NewEntity.GetComponent<Transform>();

	if (!NewEntityTransform.ParentTransform && !(NewEntity.GetId() == RootTransform->GetId()))
	{
		NewEntityTransform.SetParent(*GetRootTransform());
	}
}

Transform* SceneGraph::GetRootTransform()
{
	OPTICK_EVENT("Transform::GetRootTransform");
	if (RootTransform)
	{
		return &RootTransform->GetComponent<Transform>();
	}
	return nullptr;
}

void SceneGraph::OnEntityRemoved(Entity& InEntity)
{
	
}

void SceneGraph::OnEntityDestroyed(Entity& InEntity)
{
	Transform& transform = InEntity.GetComponent<Transform>();
	if (transform.ParentTransform.get())
	{
		transform.ParentTransform->RemoveChild(&transform);
	}
}

bool SceneGraph::OnEvent(const BaseEvent& evt)
{
	//if (evt.GetEventId() == SceneLoadedEvent::GetEventId())
	//{
	//	auto Entities = GetEntities();
	//	for (auto& ent : Entities)
	//	{
	//		Transform& transform = ent.GetComponent<Transform>();
	//		if (!transform.ParentTransform)
	//		{
	//			transform.SetParent(*RootTransform);
	//		}
	//	}
	//}
	return false;
}

#if ME_EDITOR

void SceneGraph::OnEditorInspect()
{
	BaseCore::OnEditorInspect();
}

#endif
