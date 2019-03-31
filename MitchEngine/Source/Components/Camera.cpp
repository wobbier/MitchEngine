#include "PCH.h"
#include "Camera.h"

Camera* Camera::CurrentCamera = nullptr;

Camera::Camera()
{
	Position = Vector3(0.f, 0.f, 2.f);
	Up = Vector3(0.f, 1.f, 0.f);
	Front = Vector3(0.f, 0.f, -1.f);
}

void Camera::Init()
{
	if (!CurrentCamera)
	{
		CurrentCamera = this;
	}
}

Matrix4 Camera::GetViewMatrix()
{
	return Matrix4(glm::lookAt(Position.GetInternalVec(), Position.GetInternalVec() + Front.GetInternalVec(), Up.GetInternalVec()));
}

void Camera::UpdateCameraTransform(Vector3 NewPosition)
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

float Camera::GetFOV()
{
	return m_FOV;
}