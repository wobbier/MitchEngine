#include "PCH.h"
#include "Cores/SceneGraph.h"
#include "Components/Transform.h"
#include "Engine/World.h"
#include <stack>
#include "Events/SceneEvents.h"

SceneGraph::SceneGraph()
	: Base(ComponentFilter().Requires<Transform>())
{
	IsSerializable = false;

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

void SceneGraph::Update(float dt)
{
	OPTICK_EVENT("SceneGraph::Update");

	// Seems O.K. for now
	UpdateRecursively(GetRootTransform());
}

void SceneGraph::UpdateRecursively(Transform* CurrentTransform)
{
	OPTICK_EVENT("SceneGraph::UpdateRecursively");
	for (SharedPtr<Transform> Child : CurrentTransform->GetChildren())
	{
		OPTICK_EVENT("SceneGraph::UpdateRecursively::GetChildren");
		if (Child->IsDirty())
		{
			OPTICK_EVENT("SceneGraph::Update::IsDirty");
			//Quaternion quat = Quaternion(Child->Rotation);
			DirectX::SimpleMath::Matrix id = DirectX::XMMatrixIdentity();
			DirectX::SimpleMath::Matrix rot = DirectX::SimpleMath::Matrix::CreateFromQuaternion(Child->InternalRotation.GetInternalVec());// , Child->Rotation.Y(), Child->Rotation.Z());
			DirectX::SimpleMath::Matrix scale = DirectX::SimpleMath::Matrix::CreateScale(Child->GetScale().GetInternalVec());
			DirectX::SimpleMath::Matrix pos = XMMatrixTranslationFromVector(Child->GetPosition().GetInternalVec());
			Child->SetWorldTransform(Matrix4((scale* rot * pos) * CurrentTransform->WorldTransform.GetInternalMatrix()));
		}
		UpdateRecursively(Child.get());
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
	if (transform.ParentTransform)
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