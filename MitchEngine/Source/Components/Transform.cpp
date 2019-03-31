#include "PCH.h"
#include "Transform.h"
#include <algorithm>
#include <gtc/matrix_transform.hpp>
#include "Math/Vector3.h"
#include "imgui.h"

Transform::Transform()
	: WorldTransform(1.f)
	, Position(0.f, 0.f, 0.f)
	, Rotation(0.f, 0.f, 0.f, 1.0f)
	, Scale(1.0f, 1.0f, 1.0f)
{
}


Transform::Transform(const std::string& TransformName)
	: WorldTransform(1.f)
	, Name(std::move(TransformName))
	, Position(0.f, 0.f, 0.f)
	, Rotation(0.f, 0.f, 0.f, 1.0f)
	, Scale(1.0f, 1.0f, 1.0f)
{
}

Transform::~Transform()
{
}

void Transform::SetPosition(Vector3 NewPosition)
{
	Position = NewPosition;
	SetDirty(true);
}


void Transform::SetScale(Vector3 NewScale)
{
	Scale = NewScale;
	SetDirty(true);
}

void Transform::SetScale(float NewScale)
{
	Scale = Vector3(NewScale);
	SetDirty(true);
}

void Transform::Translate(Vector3 NewPosition)
{
	Position += NewPosition;
	SetDirty(true);
}

Vector3 Transform::GetPosition()
{
	return Position;
}

void Transform::OnEditorInspect()
{
	ImGui::Text("X: %f", GetPosition().X());
	ImGui::SameLine();
	ImGui::Text("Y: %f", GetPosition().Y());
	ImGui::SameLine();
	ImGui::Text("Z: %f", GetPosition().Z());
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
void Transform::SetRotation(Vector3 euler)
{
	//glm::rotate(Rotation, quat);
	Rotation = glm::rotate(glm::quat(), euler.GetInternalVec());
	SetDirty(true);
}

void Transform::SetRotation(glm::quat quat)
{
	//glm::rotate(Rotation, quat);
	Rotation = quat;
	SetDirty(true);
}

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
	Children.erase(std::remove(Children.begin(), Children.end(), TargetTransform), Children.end());
}

DirectX::XMMATRIX Transform::GetMatrix()
{
	DirectX::XMMATRIX mat = DirectX::XMMATRIX(
		WorldTransform[0][0], WorldTransform[0][1], WorldTransform[0][2], WorldTransform[0][3],
		WorldTransform[1][0], WorldTransform[1][1], WorldTransform[1][2], WorldTransform[1][3],
		WorldTransform[2][0], WorldTransform[2][1], WorldTransform[2][2], WorldTransform[2][3],
		WorldTransform[3][0], WorldTransform[3][1], WorldTransform[3][2], WorldTransform[3][3]);
	//DirectX::XMMatrixIdentity();
	return mat;
}
