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
	RootEntity.AddComponent<Transform>();
}

void SceneGraph::Update(float dt)
{
	Transform* RootEntityTransform = &RootEntity.GetComponent<Transform>();
	std::stack<Transform*> TransformStack;
	TransformStack.push(RootEntityTransform);
	while (!TransformStack.empty())
	{
		Transform* CurrentTransform = TransformStack.top();
		TransformStack.pop();
		for (Transform* Child : CurrentTransform->Children)
		{
			if (Child->IsDirty)
			{
				glm::mat4 mat = Child->LocalTransform;
				mat = CurrentTransform->WorldTransform * mat;
				Child->SetWorldTransform(mat);
			}
			TransformStack.push(Child);
		}
	}
}

void SceneGraph::OnEntityAdded(Entity& NewEntity)
{
	Transform& NewEntityTransform = NewEntity.GetComponent<Transform>();

	if (NewEntityTransform.ParentTransform == nullptr && NewEntity != RootEntity)
	{
		NewEntityTransform.SetParent(RootEntity.GetComponent<Transform>());
	}
}
