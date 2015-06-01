#pragma once
#include <glm.hpp>
#include <gtc/matrix_transform.hpp>
namespace ma {
	class Camera {
	public:
		static Camera* CurrentCamera;

		glm::vec3 Position;
		glm::vec3 Front;
		glm::vec3 Up;
		float Zoom = 45.0f;

		Camera() {
			Up = glm::vec3(0.f, 1.f, 0.f);
			Front = glm::vec3(0.f, 0.f, -1.f);
		}
		~Camera() = default;

		glm::mat4 GetViewMatrix() {
			return glm::lookAt(this->Position, this->Position + this->Front, this->Up);
		}
	};

	Camera* Camera::CurrentCamera = nullptr;
}