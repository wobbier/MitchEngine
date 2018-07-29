#pragma once
#include "Engine/Component.h"

#define GLM_ENABLE_EXPERIMENTAL 1
#include <glm.hpp>
#include <gtx/quaternion.hpp>
#include "Utility/Util.h"

class Transform :
	public Component<Transform>
{
	friend class SceneGraph;
	friend class PhysicsCore;
public:
	Transform();
	Transform(const std::string& Name);
	virtual ~Transform();

	void SetPosition(glm::vec3 NewPosition);
	void SetScale(glm::vec3 NewScale);
	void Translate(glm::vec3 NewTransform);
	glm::vec3 GetPosition();

	MAN_NONCOPYABLE(Transform)
	MAN_NONMOVABLE(Transform)

	// Separate init from construction code.
	virtual void Init() final;

	void SetParent(Transform& NewParent);
	void RemoveChild(Transform* TargetTransform);
	glm::mat4 LocalTransform;
	glm::mat4 WorldTransform;
	std::string Name;
private:
	void SetWorldTransform(glm::mat4& NewWorldTransform);
	void SetDirty(bool Dirty);
	bool IsDirty = true;
	Transform* ParentTransform = nullptr;
	std::vector<Transform*> Children;
};
