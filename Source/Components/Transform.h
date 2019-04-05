#pragma once
#include "ECS/Component.h"

#define GLM_ENABLE_EXPERIMENTAL 1
#include <glm.hpp>
#include <gtx/quaternion.hpp>
#include "Dementia.h"
#include <DirectXMath.h>
#include "Math/Vector3.h"
#include "Havana.h"

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

	void SetPosition(Vector3 NewPosition);
	void SetRotation(Vector3 euler);
	void SetRotation(glm::quat quat);
	void SetScale(Vector3 NewScale);
	void SetScale(float NewScale);
	void Translate(Vector3 NewTransform);
	Vector3& GetPosition();

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
	
private:
#if ME_EDITOR
	virtual void OnEditorInspect() final
	{
		Havana::EditableVector3("Position", GetPosition());
	}

	virtual void Serialize(json& outJson) final
	{
		Component::Serialize(outJson);

		outJson["Position"] = { Position.X(),Position.Y(),Position.Z() };
		outJson["Rotation"] = { Rotation.x, Rotation.y, Rotation.z, Rotation.w };
	}
#endif

	void SetWorldTransform(glm::mat4& NewWorldTransform);
	void SetDirty(bool Dirty);
	bool IsDirty = true;
	Transform* ParentTransform = nullptr;
	std::vector<Transform*> Children;
};
