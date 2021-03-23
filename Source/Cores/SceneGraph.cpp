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
#include <Work/Job.h>

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

void UpdateRecursively(Transform* CurrentTransform, bool isParentDirty, Job* parentJob, bool isBursting)
{
	OPTICK_EVENT("SceneGraph::UpdateRecursively");
	auto [worker, pool] = GetEngine().GetJobSystemNew();
	auto& Children = CurrentTransform->GetChildren();
	for (const SharedPtr<Transform>& Child : Children)
	{
		bool newParentDirty = isParentDirty || Child->IsDirty();
		if (newParentDirty)
		{
			OPTICK_CATEGORY("Update Transform", Optick::Category::Scene);
			glm::mat4 model = glm::mat4(1.f);
			{
				OPTICK_CATEGORY("GLM MAT", Optick::Category::Debug);

				model = glm::translate(model, Child->GetPosition().InternalVector);
				model = glm::rotate(model, Child->GetLocalRotation().ToAngle(), Child->GetLocalRotation().ToAxis().InternalVector);
				model = glm::scale(model, Child->GetScale().InternalVector);
			}
			
			{
				OPTICK_CATEGORY("GLM MAT MULT", Optick::Category::Debug);

				Matrix4 xxx = CurrentTransform->GetMatrix().GetInternalMatrix() * model;
				Child->SetWorldTransform(xxx);
			}

			if (!Child->GetChildren().empty())
			{
				Job* job = pool.CreateClosureJobAsChild([&Child, newParentDirty, CurrentTransform](Job& job) {
					UpdateRecursively(Child.get(), newParentDirty, &job, true);// *job);
				}, parentJob);
				worker->Submit(job);
			}
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
				OPTICK_EVENT("Child Job")
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

	for (int i = 0; i < 0; ++i)
	{
		OPTICK_CATEGORY("Submit Job", Optick::Category::Debug);
		Job* root = worker->GetPool().CreateJob([](Job& job) {
			OPTICK_CATEGORY("Initial Job", Optick::Category::Debug);
		});
		recursiveJob(root, 0);
		worker->Submit(root);
		worker->Wait(root);
	}

	if(true)
	{
		Job* rootJob = pool.CreateClosureJob([this](Job& job) {

		});
		Transform* CurrentTransform = GetRootTransform();
		for (const SharedPtr<Transform>& Child : CurrentTransform->GetChildren())
		{
			OPTICK_EVENT("SceneGraph::GetChildren");
			if (Child->IsDirty())
			{
				Job* job = pool.CreateClosureJobAsChild([&Child, CurrentTransform](Job& job) {
					OPTICK_CATEGORY("Update Transform", Optick::Category::Scene);
					glm::mat4 model = glm::mat4(1.f);
					model = glm::translate(model, Child->GetPosition().InternalVector);
					model = glm::rotate(model, Child->GetLocalRotation().ToAngle(), Child->GetLocalRotation().ToAxis().InternalVector);
					model = glm::scale(model, Child->GetScale().InternalVector);

					Matrix4 xxx = CurrentTransform->GetMatrix().GetInternalMatrix() * model;
					Child->SetWorldTransform(xxx);
					if (!Child->GetChildren().empty())
					{
						UpdateRecursively(Child.get(), true, &job, false);// *job);
					}
				}, rootJob);
				worker->Submit(job);
			}
		}
		worker->Submit(rootJob);
		worker->Wait(rootJob);
	}
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
