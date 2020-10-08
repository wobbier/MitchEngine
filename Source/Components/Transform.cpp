#include "PCH.h"
#include "Transform.h"
#include <algorithm>
#include "Math/Vector3.h"
#include "misc/cpp/imgui_stdlib.h"
#include "Engine/Engine.h"
#include "Mathf.h"
#include <SimpleMath.h>
#include "optick.h"

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
		ParentTransform->RemoveChild(shared_from_this().get());
	}
}

void Transform::SetPosition(Vector3 NewPosition)
{
	WorldTransform.GetInternalMatrix().Translation((Position - NewPosition).GetInternalVec());
	Position = NewPosition;
	SetDirty(true);
	//UpdateRecursively(this);
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
	if (NewScale == Scale.x && NewScale == Scale.y && NewScale == Scale.z)
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
	SetWorldPosition(Position + NewPosition);
}

Vector3 Transform::Front()
{
	//float mat1 = -;// 2, 0);
	//float mat2 = -WorldTransform.GetInternalMatrix()(2, 1);
	//float mat3 = -WorldTransform.GetInternalMatrix()(2, 2);
	return Vector3(WorldTransform.GetInternalMatrix().Forward());
}

Vector3 Transform::Up()
{
	return Vector3(WorldTransform.GetInternalMatrix().Up());
}

Vector3& Transform::GetPosition()
{
	return Position;
}

void Transform::UpdateRecursively(SharedPtr<Transform> CurrentTransform)
{
	OPTICK_EVENT("SceneGraph::UpdateRecursively");
	for (SharedPtr<Transform> Child : CurrentTransform->GetChildren())
	{
		if (Child->m_isDirty)
		{
			OPTICK_EVENT("SceneGraph::Update::IsDirty");
			//Quaternion quat = Quaternion(Child->Rotation);
			DirectX::SimpleMath::Matrix id = DirectX::XMMatrixIdentity();
			DirectX::SimpleMath::Matrix rot = DirectX::SimpleMath::Matrix::CreateFromQuaternion(DirectX::SimpleMath::Quaternion::CreateFromYawPitchRoll(Child->Rotation[1], Child->Rotation[0], Child->Rotation[2]));// , Child->Rotation.Y(), Child->Rotation.Z());
			DirectX::SimpleMath::Matrix scale = DirectX::SimpleMath::Matrix::CreateScale(Child->GetScale().GetInternalVec());
			DirectX::SimpleMath::Matrix pos = XMMatrixTranslationFromVector(Child->GetPosition().GetInternalVec());
			Child->SetWorldTransform(Matrix4((scale * rot * pos) * CurrentTransform->WorldTransform.GetInternalMatrix()));
		}
		UpdateRecursively(Child);
	}
}

void Transform::UpdateWorldTransform()
{
	DirectX::SimpleMath::Matrix id = DirectX::XMMatrixIdentity();
	DirectX::SimpleMath::Matrix rot = DirectX::SimpleMath::Matrix::CreateFromQuaternion(DirectX::SimpleMath::Quaternion::CreateFromYawPitchRoll(Rotation[1], Rotation[0], Rotation[2]));// , Child->Rotation.Y(), Child->Rotation.Z());
	DirectX::SimpleMath::Matrix scale = DirectX::SimpleMath::Matrix::CreateScale(GetScale().GetInternalVec());
	DirectX::SimpleMath::Matrix pos = XMMatrixTranslationFromVector(GetPosition().GetInternalVec());
	SetWorldTransform(Matrix4((scale * rot * pos)));

	UpdateRecursively(shared_from_this());
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

void Transform::SetWorldTransform(Matrix4& NewWorldTransform, bool InIsDirty)
{
	OPTICK_EVENT("Transform::SetWorldTransform");
	WorldTransform = std::move(NewWorldTransform);
	{
		//DirectX::SimpleMath::Quaternion quat;
		//DirectX::SimpleMath::Vector3 pos;
		//DirectX::SimpleMath::Vector3 scale;
		//NewWorldTransform.GetInternalMatrix().Decompose(scale, quat, pos);

		//InternalRotation = Quaternion(ParentTransform->GetWorldRotation().GetInternalVec() * Quaternion(quat).GetInternalVec());
		//Vector3 rotation = Quaternion::ToEulerAngles(Quaternion(quat));
		//Rotation = Vector3(Mathf::Degrees(Rotation.X()), Mathf::Degrees(Rotation.Y()), Mathf::Degrees(Rotation.Z()));
		//Position = Vector3(pos);
	}

	// update local transform
	//WorldTransform = std::move(NewWorldTransform);
	SetDirty(InIsDirty);
}

const bool Transform::IsDirty() const
{
	return m_isDirty;
}

Transform* Transform::GetParent()
{
	return GetParentTransform();
}

Matrix4 Transform::GetLocalToWorldMatrix()
{
	DirectX::SimpleMath::Matrix id = DirectX::XMMatrixIdentity();
	DirectX::SimpleMath::Matrix rot = DirectX::SimpleMath::Matrix::CreateFromYawPitchRoll(Rotation.y, Rotation.x, Rotation.z);// , Child->Rotation.Y(), Child->Rotation.Z());
	DirectX::SimpleMath::Matrix scale = DirectX::SimpleMath::Matrix::CreateScale(Scale.GetInternalVec());
	DirectX::SimpleMath::Matrix pos = XMMatrixTranslationFromVector(Position.GetInternalVec());
	if (ParentTransform)
	{
		return Matrix4((scale * rot * pos) * GetParentTransform()->WorldTransform.GetInternalMatrix());
	}
	else
	{
		return Matrix4(scale * rot * pos);
	}
}

Matrix4 Transform::GetWorldToLocalMatrix()
{
	DirectX::SimpleMath::Matrix mat;
	GetLocalToWorldMatrix().GetInternalMatrix().Invert(mat);
	return Matrix4(mat);
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
		SetRotation(Rotation);
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
	OPTICK_EVENT("Transform::SetDirty");
	if (Dirty && (Dirty != m_isDirty))
	{
		//for (SharedPtr<Transform> Child : GetChildren())
		if(ParentTransform)
		{
			ParentTransform->SetDirty(Dirty);
		}
	}
	m_isDirty = Dirty;
}

Vector3 Transform::GetScale()
{
	return Scale;
}

void Transform::LookAt(const Vector3& InDirection)
{
	Vector3 worldPos = GetWorldPosition();
	WorldTransform = Matrix4(DirectX::SimpleMath::Matrix::CreateLookAt(worldPos.GetInternalVec(), (GetWorldPosition() + InDirection).GetInternalVec(), Vector3::Up.GetInternalVec()).Transpose());

	WorldTransform.GetInternalMatrix()._41 = worldPos[0];
	WorldTransform.GetInternalMatrix()._42 = worldPos[1];
	WorldTransform.GetInternalMatrix()._43 = worldPos[2];

	DirectX::SimpleMath::Quaternion quat;
	WorldTransform.GetInternalMatrix().Decompose(DirectX::SimpleMath::Vector3(), quat, DirectX::SimpleMath::Vector3());

	Quaternion quat2(quat);

	Rotation = Quaternion::ToEulerAngles(quat2);
	Rotation = Vector3(Mathf::Degrees(Rotation.x), Mathf::Degrees(Rotation.y), Mathf::Degrees(Rotation.z));
	InternalRotation = quat2;
	SetDirty(true);
}

void Transform::SetRotation(const Vector3& euler)
{
	OPTICK_CATEGORY("Transform::Set Rotation", Optick::Category::Scene);

	DirectX::SimpleMath::Quaternion quat2 = DirectX::SimpleMath::Quaternion::CreateFromYawPitchRoll(Mathf::Radians(euler.y), Mathf::Radians(euler.x), Mathf::Radians(euler.z));
	Quaternion quat(quat2);
	InternalRotation = quat;
	Rotation = euler;
	SetDirty(true);
}

void Transform::SetWorldRotation(Quaternion InRotation)
{
	InternalRotation = Quaternion(GetParentTransform()->GetWorldRotation().GetInternalVec() * InternalRotation.GetInternalVec());
	Rotation = Quaternion::ToEulerAngles(InternalRotation);
	SetDirty(true);
}

const Vector3& Transform::GetRotation() const
{
	return Rotation;
}

Quaternion Transform::GetWorldRotation()
{
	DirectX::SimpleMath::Quaternion quat;

	WorldTransform.GetInternalMatrix().Decompose(DirectX::SimpleMath::Vector3(), quat, DirectX::SimpleMath::Vector3());
	Quaternion quat2(quat);

	return quat2;
}

Vector3 Transform::GetWorldRotationEuler()
{
	return Quaternion::ToEulerAngles(GetWorldRotation());
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
		GetParentTransform()->RemoveChild(this);
	}
	
	ParentTransform = NewParent.shared_from_this();
	GetParentTransform()->Children.push_back(shared_from_this());
	SetDirty(true);
}

void Transform::RemoveChild(Transform* TargetTransform)
{
	if (Children.size() > 0)
	{
		Children.erase(std::remove(Children.begin(), Children.end(), TargetTransform->shared_from_this()), Children.end());
		TargetTransform->ParentTransform = nullptr;
	}
}

Transform* Transform::GetChildByName(const std::string& Name)
{
	for (auto child : Children)
	{
		if (child->Name == Name)
		{
			return child.get();
		}
	}
	return nullptr;
}

const std::vector<SharedPtr<Transform>>& Transform::GetChildren() const
{
	OPTICK_CATEGORY("Transform::GetChildren", Optick::Category::GameLogic);
	return Children;

	//std::vector<Transform*> children;
	//for (auto& child : Children)
	//{
	//	if (child)
	//	{
	//		children.push_back(&child->GetComponent<Transform>());
	//	}
	//}
	//return children;
}

Transform* Transform::GetParentTransform()
{
	if (ParentTransform)
	{
		return ParentTransform.get();
	}
	return nullptr;
}

Matrix4 Transform::GetMatrix()
{
	return WorldTransform;
}

void Transform::OnSerialize(json& outJson)
{
	outJson["Position"] = { Position.x, Position.y, Position.z };
	outJson["Rotation"] = { Rotation.x, Rotation.y, Rotation.z };
	outJson["Scale"] = { Scale.x, Scale.y, Scale.z };
}

void Transform::OnDeserialize(const json& inJson)
{
	SetPosition(Vector3((float)inJson["Position"][0], (float)inJson["Position"][1], (float)inJson["Position"][2]));
	SetRotation(Vector3((float)inJson["Rotation"][0], (float)inJson["Rotation"][1], (float)inJson["Rotation"][2]));
	if (inJson.find("Scale") != inJson.end())
	{
		SetScale(Vector3((float)inJson["Scale"][0], (float)inJson["Scale"][1], (float)inJson["Scale"][2]));
	}

	DirectX::SimpleMath::Matrix id = DirectX::XMMatrixIdentity();
	DirectX::SimpleMath::Matrix rot = DirectX::SimpleMath::Matrix::CreateFromQuaternion(DirectX::SimpleMath::Quaternion::CreateFromYawPitchRoll(Rotation[1], Rotation[0], Rotation[2]));// , Child->Rotation.Y(), Child->Rotation.Z());
	DirectX::SimpleMath::Matrix scale = DirectX::SimpleMath::Matrix::CreateScale(GetScale().GetInternalVec());
	DirectX::SimpleMath::Matrix pos = XMMatrixTranslationFromVector(GetPosition().GetInternalVec());
	if (ParentTransform)
	{
		SetWorldTransform(Matrix4((scale * rot * pos) * GetParentTransform()->WorldTransform.GetInternalMatrix()), false);
	}
	else
	{
		SetWorldTransform(Matrix4(scale * rot * pos), false);
	}
}

void Transform::SetName(const std::string& name)
{
	Name = name;
}
