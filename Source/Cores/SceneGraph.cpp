#include "PCH.h"
#include "Cores/SceneGraph.h"
#include "Components/Transform.h"
#include "Engine/World.h"
#include <stack>

SceneGraph::SceneGraph() : Base(ComponentFilter().Requires<Transform>())
{
	RootTransform = new Transform("Root Entity");
}

SceneGraph::~SceneGraph()
{
	delete RootTransform;
}

void SceneGraph::Init()
{
	RootTransform->Children.clear();
}

void SceneGraph::Update(float dt)
{
	OPTICK_EVENT("SceneGraph::Update");

	// Seems O.K. for now
	UpdateRecursively(RootTransform);
}

void SceneGraph::UpdateRecursively(Transform* CurrentTransform)
{
	OPTICK_EVENT("SceneGraph::UpdateRecursively");
	for (Transform* Child : CurrentTransform->Children)
	{
		//if (Child->IsDirty)
		{
			OPTICK_EVENT("SceneGraph::Update::IsDirty");
			glm::mat4 mat = glm::mat4(1.f);
			mat = glm::translate(mat, Child->Position.GetInternalVec());
			glm::quat rot(Child->Rotation.GetInternalVec());
			mat = glm::rotate(mat, glm::angle(rot), glm::axis(rot));
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

	if (NewEntityTransform.ParentTransform == nullptr)
	{
		NewEntityTransform.SetParent(*RootTransform);
	}
}

#if ME_EDITOR

void SceneGraph::OnEditorInspect()
{
	BaseCore::OnEditorInspect();
}

#endif