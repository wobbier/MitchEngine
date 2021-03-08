#include "PCH.h"
#include "Cores/SceneGraph.h"
#include "Components/Transform.h"
#include "Engine/World.h"
#include <stack>
#include "Events/SceneEvents.h"
#include "Engine/Engine.h"
#include "optick.h"
#include "Work/Burst.h"

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

void UpdateRecursively(Transform* CurrentTransform, bool isParentDirty, bool isBurst)
{
	OPTICK_EVENT("SceneGraph::UpdateRecursively");
	for (const SharedPtr<Transform>& Child : CurrentTransform->GetChildren())
	{
		OPTICK_EVENT("SceneGraph::UpdateRecursively::GetChildren");
		if (isParentDirty || Child->IsDirty())
		{
			OPTICK_CATEGORY("Update Transform", Optick::Category::Scene);

			glm::mat4 model = glm::mat4(1.f);
			model = glm::translate(model, Child->GetPosition().InternalVector);
			model = glm::rotate(model, Child->GetWorldRotation().ToAngle(), Child->GetWorldRotation().ToAxis().InternalVector);
			model = glm::scale(model, Child->GetScale().InternalVector);

#if ME_PLATFORM_UWP || ME_PLATFORM_WIN64
			Child->SetWorldTransform(Matrix4(model * CurrentTransform->WorldTransform.GetInternalMatrix()));
#endif
			isParentDirty = true;
		}

		if (!Child->GetChildren().empty())
		{
			if (CurrentTransform->GetChildren().size() > 5 && !isBurst)
			{
				shouldBurst = true;
			}
			if (shouldBurst)
			{
				shouldBurst = false;
#if ME_PLATFORM_UWP || ME_PLATFORM_WIN64
				Burst::LambdaWorkEntry entry;
				entry.m_callBack = [Child, isParentDirty](int Index) {
					UpdateRecursively(Child.get(), isParentDirty, true);
				};

				GetEngine().GetBurstWorker().AddWork2(entry, sizeof(Burst::LambdaWorkEntry));
#endif
			}
			else
			{
				//GetEngine().GetJobQueue().AddJobBrad([Child, isParentDirty]() {
				UpdateRecursively(Child.get(), isParentDirty, isBurst);
				//});
			}
		}
	}
}


void SceneGraph::Update(float dt)
{
	OPTICK_EVENT("SceneGraph::Update");
	//auto& jobSystem = GetEngine().GetJobSystem();
	// Seems O.K. for now
	//jobSystem.GetJobQueue().Lock();

#if ME_PLATFORM_UWP || ME_PLATFORM_WIN64
	GetEngine().GetBurstWorker().PrepareWork();
	UpdateRecursively(GetRootTransform(), false, false);
	GetEngine().GetBurstWorker().FinalizeWork();
#endif
	//jobSystem.GetJobQueue().Unlock();

	//jobSystem.Wait();
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
