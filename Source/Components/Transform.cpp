#include "PCH.h"
#include "Transform.h"
#include <algorithm>
#include "Math/Vector3.h"
#include "misc/cpp/imgui_stdlib.h"
#include "Engine/Engine.h"

Transform::Transform()
	: Component("Transform")
	, WorldTransform(DirectX::XMMatrixIdentity())
	, Position(0.f, 0.f, 0.f)
	, Rotation(0.f, 0.f, 0.f)
	, Scale(1.0f, 1.0f, 1.0f)
{
}


Transform::Transform(const std::string& TransformName)
	: Component("Transform")
	, WorldTransform(DirectX::XMMatrixIdentity())
	, Name(std::move(TransformName))
	, Position(0.f, 0.f, 0.f)
	, Rotation(0.f, 0.f, 0.f)
	, Scale(1.0f, 1.0f, 1.0f)
{
}

Transform::~Transform()
{
	if (ParentTransform)
	{
		ParentTransform->RemoveChild(this);
		ParentTransform = nullptr;
	}
}

void Transform::SetPosition(Vector3 NewPosition)
{
	WorldTransform.GetInternalMatrix().Translation((Position - NewPosition).GetInternalVec());
	Position = NewPosition;
	SetDirty(true);
}


void Transform::SetScale(Vector3 NewScale)
{
	if (NewScale == Vector3())
	{
		return;
	}
	Scale = NewScale;
	SetDirty(true);
}

void Transform::SetScale(float NewScale)
{
	if (NewScale == Scale.X() && NewScale == Scale.Y() && NewScale == Scale.Z())
	{
		return;
	}
	Scale = Vector3(NewScale);
	SetDirty(true);
}

void Transform::Translate(Vector3 NewPosition)
{
	if (NewPosition == Vector3())
	{
		return;
	}
	Position += NewPosition;
	SetDirty(true);
}

Vector3& Transform::GetPosition()
{
	return Position;
}

Vector3 Transform::GetWorldPosition()
{
	return WorldTransform.GetPosition();
}

void Transform::SetWorldPosition(const Vector3& NewPosition)
{
	DirectX::SimpleMath::Matrix& mat = WorldTransform.GetInternalMatrix();

	mat._41 = NewPosition[0];
	mat._42 = NewPosition[1];
	mat._43 = NewPosition[2];

	Position += Vector3(mat._41 - Position[0], mat._42 - Position[1], mat._43 - Position[2]);

	SetDirty(true);
}

void Transform::Reset()
{
	SetWorldPosition(Vector3());
	SetRotation(Vector3());
	SetScale(1.f);
}

void Transform::SetWorldTransform(Matrix4& NewWorldTransform)
{
	// update local transform
	WorldTransform = std::move(NewWorldTransform);
	SetDirty(false);
}

void Transform::Init()
{
}

#if ME_EDITOR
void Transform::OnEditorInspect()
{
	ImGui::InputText("Name", &Name);

	Vector3 OldPosition = Position;
	HavanaUtils::EditableVector3("Position", Position);
	Vector3 OldRotation = Rotation;
	HavanaUtils::EditableVector3("Rotation", Rotation);
	if (OldRotation != Rotation || OldPosition != Position)
	{
		SetDirty(true);
	}
	HavanaUtils::EditableVector3("Scale", Scale);
	Vector3 WorldPos = GetWorldPosition();
	HavanaUtils::EditableVector3("World Position", WorldPos);
	if (WorldPos != GetWorldPosition())
	{
		SetWorldPosition(WorldPos);
	}

	if (ImGui::Button("Reset Transform"))
	{
		Reset();
	}
}
#endif

void Transform::SetDirty(bool Dirty)
{
	if (Dirty && (Dirty != IsDirty) && Children.size())
	{
		for (auto Child : Children)
		{
			if (!Child->IsDirty)
			{
				Child->SetDirty(Dirty);
			}
		}
	}
	IsDirty = Dirty;
}

Vector3 Transform::GetScale()
{
	return Scale;
}

void Transform::SetRotation(Vector3 euler)
{
	//glm::rotate(Rotation, quat);
	Rotation = euler;
	SetDirty(true);
}
//
//void Transform::SetRotation(glm::quat quat)
//{
//	//glm::rotate(Rotation, quat);
//	Rotation = glm::eulerAngles(quat);
//	SetDirty(true);
//}

void Transform::SetParent(Transform& NewParent)
{
	if (ParentTransform)
	{
		ParentTransform->RemoveChild(this);
	}
	ParentTransform = &NewParent;
	ParentTransform->Children.push_back(this);
	SetDirty(true);
}

void Transform::RemoveChild(Transform* TargetTransform)
{
	if (Children.size() > 0)
	{
		Children.erase(std::remove(Children.begin(), Children.end(), TargetTransform), Children.end());
	}
}

Transform* Transform::GetChildByName(const std::string& Name)
{
	for (auto child : Children)
	{
		if (child->Name == Name)
		{
			return child;
		}
	}
	return nullptr;
}

Matrix4 Transform::GetMatrix()
{
	return WorldTransform;
}

void Transform::SetName(const std::string& name)
{
	Name = name;
}
