#include "PCH.h"
#include "Cores/SceneGraph.h"
#include "Components/Transform.h"
#include "Engine/World.h"
#include <stack>

SceneGraph::SceneGraph()
	: Base(ComponentFilter().Requires<Transform>())
{
	IsSerializable = false;
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
		if (Child->IsDirty)
		{
			OPTICK_EVENT("SceneGraph::Update::IsDirty");
			Quaternion quat = Quaternion(Child->Rotation);
			DirectX::SimpleMath::Matrix id = DirectX::XMMatrixIdentity();
			DirectX::SimpleMath::Matrix rot = DirectX::SimpleMath::Matrix::CreateFromQuaternion(DirectX::SimpleMath::Quaternion::CreateFromYawPitchRoll(Child->Rotation[1], Child->Rotation[0], Child->Rotation[2]));// , Child->Rotation.Y(), Child->Rotation.Z());
			DirectX::SimpleMath::Matrix scale = DirectX::SimpleMath::Matrix::CreateScale(Child->GetScale().GetInternalVec());
			DirectX::SimpleMath::Matrix pos = XMMatrixTranslationFromVector(Child->GetPosition().GetInternalVec());
			Child->SetWorldTransform(Matrix4((rot *scale* pos) * CurrentTransform->WorldTransform.GetInternalMatrix()));
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