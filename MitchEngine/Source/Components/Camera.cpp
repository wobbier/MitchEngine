#include "Camera.h"

Camera* Camera::CurrentCamera = nullptr;

Camera::Camera()
{
	Up = glm::vec3(0.f, 1.f, 0.f);
	Front = glm::vec3(0.f, 0.f, -1.f);
}

void Camera::Init()
{
	if (!CurrentCamera)
	{
		CurrentCamera = this;
	}
}

glm::mat4 Camera::GetViewMatrix(const glm::vec3& Position)
{
	return glm::lookAt(Position, Position + Front, Up);
}
