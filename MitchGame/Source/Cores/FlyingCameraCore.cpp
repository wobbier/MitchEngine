#include "FlyingCameraCore.h"
#include "../Components/FlyingCamera.h"
#include "Components/Transform.h"
#include "Components/Camera.h"
#include "Engine/Input.h"

FlyingCameraCore::FlyingCameraCore() : Base(ComponentFilter().Requires<FlyingCamera>().Requires<Camera>())
{
}

FlyingCameraCore::~FlyingCameraCore()
{
}

void FlyingCameraCore::Init()
{
}

void FlyingCameraCore::Update(float dt)
{
	auto Animatables = GetEntities();
	for (auto& InEntity : Animatables)
	{
		// Get our components that our Core required
		Transform& TransformComponent = InEntity.GetComponent<Transform>();
		FlyingCamera& FlyingCameraComponent = InEntity.GetComponent<FlyingCamera>();
		Camera& CameraComponent = InEntity.GetComponent<Camera>();

		if (&CameraComponent == Camera::CurrentCamera)
		{
			float CameraSpeed = FlyingCameraComponent.FlyingSpeed * dt;
			Input& Instance = Input::Get();
			if (Instance.IsKeyDown(GLFW_KEY_W))
			{
				TransformComponent.Position += CameraSpeed * CameraComponent.Front;
			}
			if (Instance.IsKeyDown(GLFW_KEY_S))
			{
				TransformComponent.Position -= CameraSpeed * CameraComponent.Front;
			}
			if (Instance.IsKeyDown(GLFW_KEY_A))
			{
				TransformComponent.Position -= glm::normalize(glm::cross(CameraComponent.Front, CameraComponent.Up)) * CameraSpeed;
			}
			if (Instance.IsKeyDown(GLFW_KEY_D))
			{
				TransformComponent.Position += glm::normalize(glm::cross(CameraComponent.Front, CameraComponent.Up)) * CameraSpeed;
			}

			glm::vec2 MousePosition = Instance.GetMousePosition();
			if (FirstUpdate)
			{
				LastX = MousePosition.x;
				LastY = MousePosition.y;
				FirstUpdate = false;
			}

			float XOffset = MousePosition.x - LastX;
			float YOffest = LastY - MousePosition.y;
			LastX = MousePosition.x;
			LastY = MousePosition.y;

			XOffset *= FlyingCameraComponent.LookSensitivity;
			YOffest *= FlyingCameraComponent.LookSensitivity;

			const float Yaw = CameraComponent.Yaw += XOffset;
			float Pitch = CameraComponent.Pitch += YOffest;

			if (Pitch > 89.0f)
				Pitch = 89.0f;
			if (Pitch < -89.0f)
				Pitch = -89.0f;

			glm::vec3 Front;
			Front.x = cos(glm::radians(Yaw)) * cos(glm::radians(Pitch));
			Front.y = sin(glm::radians(Pitch));
			Front.z = sin(glm::radians(Yaw)) * cos(glm::radians(Pitch));
			CameraComponent.Front = glm::normalize(Front);
		}
	}
}