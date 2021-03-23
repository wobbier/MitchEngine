#include "PCH.h"
#include "Transform.h"
#include <algorithm>
#include "Math/Vector3.h"
#include "misc/cpp/imgui_stdlib.h"
#include "Engine/Engine.h"
#include "Mathf.h"
#include "optick.h"
#include <glm/gtc/type_ptr.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtc/matrix_transform.hpp>

Transform::Transform()
	: Component("Transform")
	, WorldTransform()
	, LocalPosition(0.f, 0.f, 0.f)
	, Scale(1.0f, 1.0f, 1.0f)
{
}


Transform::Transform(const std::string& TransformName)
	: Component("Transform")
	, WorldTransform()
	, Name(std::move(TransformName))
	, LocalPosition(0.f, 0.f, 0.f)
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
	//WorldTransform.GetInternalMatrix().Translation((Position - NewPosition).InternalVec);
	LocalPosition = NewPosition;
	SetDirty(true);
	//UpdateRecursively(this);
}


void Transform::SetScale(Vector3 NewScale)
{
	if (NewScale.IsZero())
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
	if (NewPosition.IsZero())
	{
		return;
	}
	SetWorldPosition(LocalPosition + NewPosition);
}

void Transform::Rotate(const Vector3& inDegrees, TransformSpace inRelativeTo /*= TransformSpace::Self*/)
{
	Quaternion eulerRot(inDegrees);
	if (inRelativeTo == TransformSpace::World)
	{
		//LocalRotation = Quaternion(LocalRotation.InternalQuat * (glm::inverse(Rotation.InternalQuat) * eulerRot.InternalQuat * Rotation.InternalQuat));
		//SetRotation(LocalRotation);
	}
	else
	{
		LocalRotation = LocalRotation * eulerRot;
		SetRotation(LocalRotation);
	}
}

Vector3 Transform::Front()
{
	glm::mat4& world = WorldTransform.GetInternalMatrix();
	//float mat1 = -;// 2, 0);
	//float mat2 = -WorldTransform.GetInternalMatrix()(2, 1);
	//float mat3 = -WorldTransform.GetInternalMatrix()(2, 2);
	// 20, 21, 22
	const float* matrix = glm::value_ptr(world);
	return Vector3(matrix[8], matrix[9], matrix[10]);// WorldTransform.GetInternalMatrix().Forward());
}

Vector3 Transform::Up()
{
	return Vector3(WorldTransform.GetInternalMatrix()[1][0], WorldTransform.GetInternalMatrix()[1][1], WorldTransform.GetInternalMatrix()[1][2]);
}

Vector3 Transform::Right()
{
	return Vector3(WorldTransform.GetInternalMatrix()[0][0], WorldTransform.GetInternalMatrix()[0][1], WorldTransform.GetInternalMatrix()[0][2]);
}

Vector3& Transform::GetPosition()
{
	return LocalPosition;
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
			//DirectX::SimpleMath::Matrix id = DirectX::XMMatrixIdentity();
			//DirectX::SimpleMath::Matrix rot = DirectX::SimpleMath::Matrix::CreateFromQuaternion(DirectX::SimpleMath::Quaternion::CreateFromYawPitchRoll(Child->LocalRotation[1], Child->LocalRotation[0], Child->LocalRotation[2]));// , Child->Rotation.Y(), Child->Rotation.Z());
			//DirectX::SimpleMath::Matrix scale = DirectX::SimpleMath::Matrix::CreateScale(Child->GetScale().InternalVec);
			//DirectX::SimpleMath::Matrix pos = XMMatrixTranslationFromVector(Child->GetPosition().InternalVec);
			//Child->SetWorldTransform(Matrix4((scale * rot * pos) * CurrentTransform->WorldTransform.GetInternalMatrix()));
		}
		UpdateRecursively(Child);
	}
}

void Transform::UpdateWorldTransform()
{
	//DirectX::SimpleMath::Matrix id = DirectX::XMMatrixIdentity();
	//DirectX::SimpleMath::Matrix rot = DirectX::SimpleMath::Matrix::CreateFromQuaternion(DirectX::SimpleMath::Quaternion::CreateFromYawPitchRoll(LocalRotation[1], LocalRotation[0], LocalRotation[2]));// , Child->Rotation.Y(), Child->Rotation.Z());
	//DirectX::SimpleMath::Matrix scale = DirectX::SimpleMath::Matrix::CreateScale(GetScale().InternalVec);
	//DirectX::SimpleMath::Matrix pos = XMMatrixTranslationFromVector(GetPosition().InternalVec);
	//SetWorldTransform(Matrix4((scale * rot * pos)));

	UpdateRecursively(shared_from_this());
}

Vector3 Transform::GetWorldPosition()
{
	return WorldTransform.GetPosition();
}

void Transform::SetWorldPosition(const Vector3& NewPosition)
{
	glm::mat4& mat = WorldTransform.GetInternalMatrix();

	mat[3][0] = NewPosition[0];
	mat[3][1] = NewPosition[1];
	mat[3][2] = NewPosition[2];

	LocalPosition += Vector3(mat[3][0] - LocalPosition[0], mat[3][1] - LocalPosition[1], mat[3][2] - LocalPosition[2]);

	SetDirty(true);
}

void Transform::Reset()
{
	SetWorldPosition(Vector3());
	SetRotation(Vector3());
	SetScale(1.f);
	SetWorldTransform(Matrix4(), true);
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

Matrix4 Transform::GetLocalToWorldMatrix()
{
	glm::mat4 id(1.f);
	id = glm::translate(id, GetPosition().InternalVector);
	id = glm::rotate(id, GetLocalRotation().ToAngle(), GetLocalRotation().ToAxis().InternalVector);
	id = glm::scale(id, GetScale().InternalVector);

	if (ParentTransform)
	{
		return Matrix4(id * GetParentTransform()->WorldTransform.GetInternalMatrix());
	}
	else
	{
		return id;
	}
}

Matrix4 Transform::GetWorldToLocalMatrix()
{
	return GetMatrix().Inverse();
}

void Transform::Init()
{
}

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
    Matrix4 worldMat(glm::transpose(glm::lookAtLH(GetWorldPosition().InternalVector, GetWorldPosition().InternalVector + InDirection.InternalVector, glm::vec3(0,1,0))));
	SetWorldTransform(worldMat);
	glm::vec3 scale;
	glm::quat rot;
	glm::vec3 pos;
	glm::vec3 skeq;
	glm::vec4 poers;
	glm::decompose(WorldTransform.GetInternalMatrix(), scale, rot, pos, skeq, poers);
    LocalRotation = Quaternion(rot);
	Rotation = LocalRotation;
	Rotation = Vector3(Mathf::Degrees(Rotation.x), Mathf::Degrees(Rotation.y), Mathf::Degrees(Rotation.z));
	//Vector3 worldPos = GetWorldPosition();
	//WorldTransform = Matrix4(DirectX::SimpleMath::Matrix::CreateLookAt(worldPos.InternalVec, (GetWorldPosition() + InDirection).InternalVec, Vector3::Up.InternalVec).Transpose());

	//WorldTransform.GetInternalMatrix()._41 = worldPos[0];
	//WorldTransform.GetInternalMatrix()._42 = worldPos[1];
	//WorldTransform.GetInternalMatrix()._43 = worldPos[2];

	//DirectX::SimpleMath::Quaternion quat;
	//WorldTransform.GetInternalMatrix().Decompose(DirectX::SimpleMath::Vector3(), quat, DirectX::SimpleMath::Vector3());

	//Quaternion quat2(quat);

	//Rotation = Quaternion::ToEulerAngles(quat2);
	//Rotation = Vector3(Mathf::Degrees(Rotation.x), Mathf::Degrees(Rotation.y), Mathf::Degrees(Rotation.z));
	//LocalRotation = quat2;
	SetDirty(true);
}

void Transform::SetRotation(const Vector3& euler)
{
	OPTICK_CATEGORY("Transform::Set Rotation", Optick::Category::Scene);

	LocalRotation.InternalQuat = glm::quat(glm::vec3(Mathf::Radians(euler.x), Mathf::Radians(euler.y), Mathf::Radians(euler.z)));
	//DirectX::SimpleMath::Quaternion quat2 = DirectX::SimpleMath::Quaternion::CreateFromYawPitchRoll(Mathf::Radians(euler.y), Mathf::Radians(euler.x), Mathf::Radians(euler.z));
	//Quaternion quat(quat2);
	//LocalRotation = quat;
	Rotation = euler;
	SetDirty(true);
}

void Transform::SetRotation(Quaternion InRotation)
{
	LocalRotation = InRotation;
	Rotation = Quaternion::ToEulerAngles(LocalRotation);
	SetDirty(true);
}

Vector3 Transform::GetRotationEuler() const
{
	return Quaternion::ToEulerAngles(LocalRotation);
}

Quaternion Transform::GetLocalRotation() const
{
	return LocalRotation;
}

Quaternion Transform::GetWorldRotation()
{
	return WorldTransform.GetRotation();
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

Matrix4& Transform::GetMatrix()
{
	return WorldTransform;
}

const std::string& Transform::GetName() const
{
	return Name;
}

void Transform::OnSerialize(json& outJson)
{
	outJson["Position"] = { LocalPosition.x, LocalPosition.y, LocalPosition.z };
	outJson["Rotation"] = { LocalRotation.x, LocalRotation.y, LocalRotation.z };
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

	//DirectX::SimpleMath::Matrix id = DirectX::XMMatrixIdentity();
	//DirectX::SimpleMath::Matrix rot = DirectX::SimpleMath::Matrix::CreateFromQuaternion(DirectX::SimpleMath::Quaternion::CreateFromYawPitchRoll(LocalRotation[1], LocalRotation[0], LocalRotation[2]));// , Child->Rotation.Y(), Child->Rotation.Z());
	//DirectX::SimpleMath::Matrix scale = DirectX::SimpleMath::Matrix::CreateScale(GetScale().InternalVec);
	//DirectX::SimpleMath::Matrix pos = XMMatrixTranslationFromVector(GetPosition().InternalVec);
	//if (ParentTransform)
	//{
	//	SetWorldTransform(Matrix4((scale * rot * pos) * GetParentTransform()->WorldTransform.GetInternalMatrix()), false);
	//}
	//else
	//{
	//	SetWorldTransform(Matrix4(scale * rot * pos), false);
	//}
}

void Transform::SetName(const std::string& name)
{
	Name = name;
}

//#if ME_EDITOR

void Transform::OnEditorInspect()
{
	ImGui::InputText("Name", &Name);

	Vector3 OldPosition = LocalPosition;
	if (HavanaUtils::EditableVector3("Local Position", OldPosition))
	{
		SetPosition(OldPosition);
	}

	Vector3 OldRotation = Quaternion::ToEulerAngles(GetLocalRotation());
	if (HavanaUtils::EditableVector3("Local Rotation", OldRotation))
	{
		SetRotation(OldRotation);
	}

	Vector3 OldScale = Scale;
	if (HavanaUtils::EditableVector3("Scale", OldScale))
	{
		SetScale(OldScale);
	}

	Vector3 WorldPos = GetWorldPosition();
	if(HavanaUtils::EditableVector3("World Position", WorldPos))
	{
		SetWorldPosition(WorldPos);
	}

	Vector3 OldWorldRotation = GetWorldRotationEuler();
	if (HavanaUtils::EditableVector3("World Rotation", OldWorldRotation))
	{
		//SetWorldRotation(OldRotation);
	}

	if (ImGui::Button("Reset Transform"))
	{
		Reset();
	}
}

//#endif
