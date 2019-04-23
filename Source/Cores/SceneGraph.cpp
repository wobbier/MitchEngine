#include "PCH.h"
#include "Cores/SceneGraph.h"
#include "Components/Transform.h"
#include "Engine/World.h"
#include <stack>

SceneGraph::SceneGraph() : Base(ComponentFilter().Requires<Transform>())
{
}

SceneGraph::~SceneGraph()
{
}

void SceneGraph::Init()
{
	RootEntity = GetWorld().CreateEntity();
	RootEntity.lock()->AddComponent<Transform>("Root Entity");
}

void SceneGraph::Update(float dt)
{
	BROFILER_CATEGORY("SceneGraph::Update", Brofiler::Color::Green);

	// Seems O.K. for now
	if(RootEntity.lock()->HasComponent<Transform>())
	UpdateRecursively(&RootEntity.lock()->GetComponent<Transform>());
}

void SceneGraph::UpdateRecursively(Transform* CurrentTransform)
{
	BROFILER_CATEGORY("SceneGraph::UpdateRecursively", Brofiler::Color::DarkOrange);
	for (Transform* Child : CurrentTransform->Children)
	{
		if (Child->IsDirty)
		{
			BROFILER_CATEGORY("SceneGraph::Update::IsDirty", Brofiler::Color::DarkOrange);
			glm::mat4 mat = glm::translate(glm::mat4(1.0f), Child->Position.GetInternalVec());
			mat = glm::rotate(mat, glm::angle(Child->Rotation), glm::axis(Child->Rotation));
			mat = glm::scale(mat, Child->Scale.GetInternalVec());
			Child->SetWorldTransform(CurrentTransform->WorldTransform * mat);
		}
		UpdateRecursively(Child);
	}
}

void SceneGraph::OnEntityAdded(Entity& NewEntity)
{
	Base::OnEntityAdded(NewEntity);

	Transform& NewEntityTransform = NewEntity.GetComponent<Transform>();

	if (NewEntityTransform.ParentTransform == nullptr && NewEntity != *RootEntity.lock().get())
	{
		NewEntityTransform.SetParent(RootEntity.lock()->GetComponent<Transform>());
	}
}

#if ME_EDITOR

void SceneGraph::OnEditorInspect()
{
	BaseCore::OnEditorInspect();
}

#endif