#include "Camera.h"

Camera* Camera::CurrentCamera = nullptr;

Camera::Camera()
{
	Position = glm::vec3(0.f, 0.f, 2.f);
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

glm::mat4 Camera::GetViewMatrix()
{
	return glm::lookAt(Position, Position + Front, Up);
}

void Camera::UpdateCameraTransform(glm::vec3 NewPosition)
{
	Position = NewPosition;
}

bool Camera::IsCurrent()
{
	return Camera::CurrentCamera == this;
}

void Camera::SetCurrent()
{
	CurrentCamera = this;
}
