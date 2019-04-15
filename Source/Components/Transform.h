#pragma once
#include "ECS/Component.h"

#define GLM_ENABLE_EXPERIMENTAL 1
#include <glm.hpp>
#include <gtx/quaternion.hpp>
#include "Dementia.h"
#include <DirectXMath.h>
#include "Math/Vector3.h"
#include "Havana.h"
#include "ECS/ComponentDetail.h"

class Transform :
	public Component<Transform>
{
	typedef Component<Transform> Base;
	friend class Havana;
	friend class EditorCore;
	friend class SceneGraph;
	friend class PhysicsCore;
public:
	Transform();
	Transform(const std::string& Name);
	virtual ~Transform();

	// Local Space
	void SetPosition(Vector3 NewPosition);
	Vector3& GetPosition();

	void SetRotation(Vector3 euler);
	void SetRotation(glm::quat quat);

	void SetScale(Vector3 NewScale);
	void SetScale(float NewScale);

	void Translate(Vector3 NewTransform);

	//World Space
	Vector3 GetWorldPosition();
	void SetWorldPosition(const Vector3& NewPosition);

	ME_NONCOPYABLE(Transform)
	ME_NONMOVABLE(Transform)

	// Separate init from construction code.
	virtual void Init() final;

	void SetParent(Transform& NewParent);
	void RemoveChild(Transform* TargetTransform);
	Vector3 Position;
	Vector3 Scale;
	glm::quat Rotation;

	glm::mat4 WorldTransform;
	std::string Name;
	DirectX::XMMATRIX GetMatrix();

	virtual void Serialize(json& outJson) final
	{
		Component::Serialize(outJson);

		outJson["Position"] = { Position.X(),Position.Y(),Position.Z() };
		outJson["Rotation"] = { Rotation.x, Rotation.y, Rotation.z, Rotation.w };
	}

	virtual void Deserialize(const json& inJson) final
	{
		SetPosition(Vector3((float)inJson["Position"][0], (float)inJson["Position"][1], (float)inJson["Position"][2]));
	}
	
	void SetName(const std::string& name);
private:
#if ME_EDITOR
	virtual void OnEditorInspect() final
	{
		Havana::EditableVector3("Position", GetPosition());
		Havana::EditableVector3("Scale", Scale);
		Vector3 WorldPos = GetWorldPosition();
		Havana::EditableVector3("World Position", WorldPos);
		if (WorldPos != GetWorldPosition())
		{
			SetWorldPosition(WorldPos);
		}
	}

#endif

	void SetWorldTransform(glm::mat4& NewWorldTransform);
	void SetDirty(bool Dirty);
	bool IsDirty = true;
	Transform* ParentTransform = nullptr;
	std::vector<Transform*> Children;
};
ME_REGISTER_COMPONENT(Transform)
