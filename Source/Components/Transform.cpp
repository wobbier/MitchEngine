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
#include <glm/gtx/quaternion.hpp>

Transform::Transform()
	: Component("Transform")
	, LocalPosition(0.f, 0.f, 0.f)
	, LocalScale(1.0f, 1.0f, 1.0f)
{
}


Transform::Transform(const std::string& TransformName)
	: Component("Transform")
	, Name(std::move(TransformName))
	, LocalPosition(0.f, 0.f, 0.f)
	, LocalScale(1.0f, 1.0f, 1.0f)
{
}

Transform::~Transform()
{
	if (ParentTransform)
	{
		ParentTransform->RemoveChild(shared_from_this().get());
	}
	ParentTransform = nullptr;
}

void Transform::Init()
{
}

Vector3 Transform::GetPosition() const
{
	return LocalPosition;
}

void Transform::SetPosition(const Vector3& NewPosition)
{
	LocalPosition = NewPosition;
	SetDirty(true);
}

Quaternion Transform::GetRotation() const
{
	return LocalRotation;
}

void Transform::SetRotation(const Quaternion& InRotation)
{
	LocalRotation = InRotation;
	SetDirty(true);
}

Vector3 Transform::GetScale()
{
	return LocalScale;
}

void Transform::SetScale(const Vector3& NewScale)
{
	if (NewScale.IsZero())
	{
		return;
	}
	LocalScale = NewScale;
	SetDirty(true);
}

void Transform::SetScale(float NewScale)
{
	if (NewScale == LocalScale.x && NewScale == LocalScale.y && NewScale == LocalScale.z)
	{
		return;
	}
	LocalScale = Vector3(NewScale);
	SetDirty(true);
}

Vector3 Transform::GetWorldPosition() const
{
	if (ParentTransform)
	{
		return ParentTransform->GetLocalToWorldMatrix().TransformPoint(GetPosition());
	}
	return GetPosition();
}

void Transform::SetWorldPosition(const Vector3& NewPosition)
{
	if (ParentTransform)
	{
		LocalPosition = ParentTransform->GetWorldToLocalMatrix().TransformPoint(NewPosition);
	}
	else
	{
		LocalPosition = NewPosition;
	}

	SetDirty(true);
}

Quaternion Transform::GetWorldRotation()
{
	if (ParentTransform)
	{
		return ParentTransform->GetWorldRotation() * GetRotation();
	}

	return GetRotation();
}

void Transform::SetWorldRotation(const Quaternion& inRotation)
{
	if (ParentTransform)
	{
		LocalRotation = ParentTransform->GetWorldRotation().Inverse() * inRotation;
	}
	else
	{
		LocalRotation = inRotation;
	}
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
	return GetWorldRotation().Rotate(Vector3::Front);
	//const glm::mat4& world = WorldTransform.GetInternalMatrix();
	////float mat1 = -;// 2, 0);
	////float mat2 = -WorldTransform.GetInternalMatrix()(2, 1);
	////float mat3 = -WorldTransform.GetInternalMatrix()(2, 2);
	//// 20, 21, 22
	//const float* matrix = glm::value_ptr(world);
	//return Vector3(matrix[8], matrix[9], matrix[10]);// WorldTransform.GetInternalMatrix().Forward());
}

Vector3 Transform::Up()
{
	return GetWorldRotation().Rotate(Vector3::Up);
	//return Vector3(WorldTransform.GetInternalMatrix()[1][0], WorldTransform.GetInternalMatrix()[1][1], WorldTransform.GetInternalMatrix()[1][2]);
}

Vector3 Transform::Right()
{
	return GetWorldRotation().Rotate(Vector3::Right);
	//return Vector3(WorldTransform.GetInternalMatrix()[0][0], WorldTransform.GetInternalMatrix()[0][1], WorldTransform.GetInternalMatrix()[0][2]);
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

void Transform::Reset()
{
	SetWorldPosition(Vector3());
	SetRotation(Vector3());
	SetScale(1.f);
    Matrix4 idMat;
	SetWorldTransform(idMat, true);
}

void Transform::SetWorldTransform(Matrix4& NewWorldTransform, bool InIsDirty)
{
	OPTICK_EVENT("Transform::SetWorldTransform");
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
	return IsLocalToWorldDirty || IsWorldToLocalDirty;
}

const Matrix4& Transform::GetLocalToWorldMatrix()
{
	if (IsLocalToWorldDirty)
	{
		glm::mat4 T = glm::translate(glm::mat4(1.0f), GetPosition().InternalVector);
		glm::quat R = GetRotation().InternalQuat;
		glm::mat4 S = glm::scale(glm::mat4(1.0f), GetScale().InternalVector);
		glm::mat4 P = ParentTransform ? ParentTransform->GetLocalToWorldMatrix().GetInternalMatrix() : glm::mat4(1.0f);

		LocalToWorldMatrix = Matrix4(P * T * glm::toMat4(R) * S);
		IsLocalToWorldDirty = false;
	}

	return LocalToWorldMatrix;
	//id = glm::scale(id, GetScale().InternalVector);
	//id = glm::rotate(id, GetLocalRotation().ToAngle(), GetLocalRotation().ToAxis().InternalVector);
	//id = glm::translate(id, GetPosition().InternalVector);
	//
	//if (ParentTransform)
	//{
	//	return Matrix4(GetParentTransform()->WorldTransform.GetInternalMatrix() * id);
	//}
	//else
	//{
	//	return Matrix4(id);
	//}
}

const Matrix4& Transform::GetWorldToLocalMatrix()
{
	if (IsWorldToLocalDirty)
	{
		WorldToLocalMatrix = GetLocalToWorldMatrix().Inverse();
		IsWorldToLocalDirty = false;
	}
	return WorldToLocalMatrix;
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
	IsLocalToWorldDirty = true;
	IsWorldToLocalDirty = true;
}

void Transform::LookAt(const Vector3& InDirection)
{
    //Matrix4 worldMat(glm::transpose(glm::lookAtLH(GetWorldPosition().InternalVector, GetWorldPosition().InternalVector + InDirection.InternalVector, glm::vec3(0,1,0))));
	//SetWorldTransform(worldMat);
	//glm::vec3 scale;
	//glm::quat rot;
	//glm::vec3 pos;
	//glm::vec3 skeq;
	//glm::vec4 poers;
	//glm::decompose(WorldTransform.GetInternalMatrix(), scale, rot, pos, skeq, poers);
    //LocalRotation = Quaternion(rot);
	//Rotation = LocalRotation;
	//Rotation = Vector3(Mathf::Degrees(Rotation.x), Mathf::Degrees(Rotation.y), Mathf::Degrees(Rotation.z));
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
	SetDirty(true);
}

Vector3 Transform::GetRotationEuler() const
{
	return Quaternion::ToEulerAngles(LocalRotation);
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

const Matrix4& Transform::GetMatrix()
{
	return GetLocalToWorldMatrix();
}

const std::string& Transform::GetName() const
{
	return Name;
}

void Transform::OnSerialize(json& outJson)
{
	outJson["Position"] = { LocalPosition.x, LocalPosition.y, LocalPosition.z };
	Vector3 outRot = Quaternion::ToEulerAngles(LocalRotation);
	outJson["Rotation"] = { outRot.x, outRot.y, outRot.z };
	outJson["Scale"] = { LocalScale.x, LocalScale.y, LocalScale.z };
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
	HavanaUtils::Label("Name");
	ImGui::InputText("##Name", &Name);

	Vector3 OldPosition = LocalPosition;
	if (HavanaUtils::EditableVector3("Local Position", OldPosition))
	{
		SetPosition(OldPosition);
	}

	Vector3 OldRotation = Quaternion::ToEulerAngles(GetRotation());
	if (HavanaUtils::EditableVector3("Local Rotation", OldRotation))
	{
		SetRotation(OldRotation);
	}

	Vector3 OldScale = LocalScale;
	if (HavanaUtils::EditableVector3("Scale", OldScale, 1.f))
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

	ImGui::Text(GetLocalToWorldMatrix().ToString().c_str());
	ImGui::Separator();

	ImGui::Text(GetWorldToLocalMatrix().ToString().c_str());

}

//#endif
