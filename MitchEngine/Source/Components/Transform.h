#pragma once
#include "ECS/Component.h"

#define GLM_ENABLE_EXPERIMENTAL 1
#include <glm.hpp>
#include <gtx/quaternion.hpp>
#include "Dementia.h"
#include <DirectXMath.h>
#include "Math/Vector3.h"

class Transform :
	public Component<Transform>
{
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
	Vector3 GetPosition();

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
	void SetWorldTransform(glm::mat4& NewWorldTransform);
	void SetDirty(bool Dirty);
	bool IsDirty = true;
	Transform* ParentTransform = nullptr;
	std::vector<Transform*> Children;
};
