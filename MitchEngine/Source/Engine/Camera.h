#pragma once
#include <glm.hpp>
#include <gtc/matrix_transform.hpp>

class Camera
{
public:
	static Camera* CurrentCamera;

	glm::vec3 Position;
	glm::vec3 Front;
	glm::vec3 Up;
	float Zoom = 45.0f;

	Camera()
	{
		Position = glm::vec3(0.f, 0.f, 3.f);
		Up = glm::vec3(0.f, 1.f, 0.f);
		Front = glm::vec3(0.f, 0.f, -1.f);
	}
	~Camera() = default;

	glm::mat4 GetViewMatrix()
	{
		return glm::lookAt(Position, Position + Front, Up);
	}
};

Camera* Camera::CurrentCamera = nullptr;
