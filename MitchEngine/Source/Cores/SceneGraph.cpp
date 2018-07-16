#include "PCH.h"
#include "Cores/SceneGraph.h"
#include "Components/Transform.h"
#include "Engine/World.h"
#include <stack>
#include "Graphics/UI/imgui.h"

SceneGraph::SceneGraph() : Base(ComponentFilter().Requires<Transform>())
{
}

SceneGraph::~SceneGraph()
{
}

void SceneGraph::Init()
{
	RootEntity = GetWorld().CreateEntity();
	RootEntity.AddComponent<Transform>("Root Entity");
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
	ImGui::Begin("Scene Graph");
	UpdateUI(RootEntityTransform);
	ImGui::End();
}

void SceneGraph::OnEntityAdded(Entity& NewEntity)
{
	Transform& NewEntityTransform = NewEntity.GetComponent<Transform>();

	if (NewEntityTransform.ParentTransform == nullptr && NewEntity != RootEntity)
	{
		NewEntityTransform.SetParent(RootEntity.GetComponent<Transform>());
	}
}

void SceneGraph::UpdateUI(Transform* StartingTransform)
{
	bool ExpandChildren = false;
	for (Transform* Child : StartingTransform->Children)
	{
		ExpandChildren = ImGui::TreeNode(Child->Name.c_str());
		if (ExpandChildren)
		{
			UpdateUI(Child);
			ImGui::TreePop();
		}
	}
}
