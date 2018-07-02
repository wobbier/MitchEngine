#pragma once
#include "Engine/Component.h"

#define GLM_ENABLE_EXPERIMENTAL 1
#include <glm.hpp>
#include <gtx/quaternion.hpp>

class Transform :
	public Component<Transform>
{
public:
	glm::vec3 Position;
	glm::vec3 Scale;
	glm::quat Rotation;

	Transform();
	virtual ~Transform();

	// Separate init from construction code.
	virtual void Init() final;
};
