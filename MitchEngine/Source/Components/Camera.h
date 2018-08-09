#pragma once
#include <glm.hpp>
#include <gtc/matrix_transform.hpp>

#include "ECS/Component.h"
#include "Components/Transform.h"

class Camera
	: public Component<Camera>
{
public:
	static Camera* CurrentCamera;

	glm::vec3 Position;
	glm::vec3 Front;
	glm::vec3 Up;
	float Zoom = 45.0f;
	float Yaw = -90.f;
	float Pitch = 0.f;

	Camera();
	~Camera() = default;

	virtual void Init() override;

	glm::mat4 GetViewMatrix();
	void UpdateCameraTransform(glm::vec3 TransformComponent);
	bool IsCurrent();
	void SetCurrent();
};
