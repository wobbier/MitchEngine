#pragma once
#include <glm.hpp>
#include <gtc/matrix_transform.hpp>

#include "Engine/Component.h"

class Camera
	: public Component<Camera>
{
public:
	static Camera* CurrentCamera;

	glm::vec3 Front;
	glm::vec3 Up;
	float Zoom = 45.0f;

	Camera();
	~Camera() = default;

	virtual void Init() override;

	glm::mat4 GetViewMatrix(const glm::vec3& Position);
};
