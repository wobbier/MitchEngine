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
public:
	Transform();
	virtual ~Transform();

	void SetPosition(glm::vec3 NewPosition);
	void Translate(glm::vec3 NewPosition);
	glm::vec3 GetPosition();

	MA_NONCOPYABLE(Transform);
	MA_NONMOVABLE(Transform);

	// Separate init from construction code.
	virtual void Init() final;

	void SetParent(Transform& NewParent);
	void RemoveChild(Transform* TargetTransform);
	glm::mat4 LocalTransform;
	glm::mat4 WorldTransform;
private:
	void SetWorldTransform(glm::mat4& NewWorldTransform);
	void SetDirty(bool Dirty);
	bool IsDirty = true;
	Transform* ParentTransform = nullptr;
	std::vector<Transform*> Children;
};
