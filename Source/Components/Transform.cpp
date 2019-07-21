#include "PCH.h"
#include "Transform.h"
#include <algorithm>
#include <gtc/matrix_transform.hpp>
#include "Math/Vector3.h"
#include "misc/cpp/imgui_stdlib.h"

Transform::Transform()
	: WorldTransform(1.f)
	, Position(0.f, 0.f, 0.f)
	, Rotation(0.f, 0.f, 0.f)
	, Scale(1.0f, 1.0f, 1.0f)
{
}


Transform::Transform(const std::string& TransformName)
	: WorldTransform(1.f)
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
	return Vector3(WorldTransform[3][0], WorldTransform[3][1], WorldTransform[3][2]);
}

void Transform::SetWorldPosition(const Vector3& NewPosition)
{
	WorldTransform[3][0] = NewPosition[0];
	WorldTransform[3][1] = NewPosition[1];
	WorldTransform[3][2] = NewPosition[2];

	Position += Vector3(WorldTransform[3][0] - Position[0], WorldTransform[3][1] - Position[1], WorldTransform[3][2] - Position[2]);

	SetDirty(true);
}

void Transform::Reset()
{
	SetWorldPosition(Vector3());
	SetRotation(Vector3());
	SetScale(1.f);
}

void Transform::SetWorldTransform(glm::mat4& NewWorldTransform)
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


	//HavanaUtils::EditableVector3("Position", GetPosition());
	Vector3 OldRotation = Rotation;
	HavanaUtils::EditableVector3("Rotation", Rotation);
	if (OldRotation != Rotation)
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

DirectX::XMMATRIX Transform::GetMatrix()
{
	DirectX::XMMATRIX mat = DirectX::XMMATRIX(
		WorldTransform[0][0], WorldTransform[0][1], WorldTransform[0][2], WorldTransform[0][3],
		WorldTransform[1][0], WorldTransform[1][1], WorldTransform[1][2], WorldTransform[1][3],
		WorldTransform[2][0], WorldTransform[2][1], WorldTransform[2][2], WorldTransform[2][3],
		WorldTransform[3][0], WorldTransform[3][1], WorldTransform[3][2], WorldTransform[3][3]);
	return mat;
}

void Transform::SetName(const std::string& name)
{
	Name = name;
}
