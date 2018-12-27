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
			float CameraSpeed = FlyingCameraComponent.FlyingSpeed;
			Input& Instance = Input::GetInstance();
#if ME_PLATFORM_WIN64
			if (Instance.IsKeyDown(GLFW_KEY_LEFT_SHIFT))
			{
				CameraSpeed += FlyingCameraComponent.SpeedModifier;
			}
			CameraSpeed *= dt;
			if (Instance.IsKeyDown(GLFW_KEY_W))
			{
				TransformComponent.SetPosition((CameraSpeed * CameraComponent.Front) + TransformComponent.GetPosition());
			}
			if (Instance.IsKeyDown(GLFW_KEY_S))
			{
				TransformComponent.SetPosition(TransformComponent.GetPosition() - (CameraSpeed * CameraComponent.Front));
			}
			if (Instance.IsKeyDown(GLFW_KEY_A))
			{
				TransformComponent.Translate(glm::normalize(glm::cross(CameraComponent.Up, CameraComponent.Front)) * CameraSpeed);
			}
			if (Instance.IsKeyDown(GLFW_KEY_D))
			{
				TransformComponent.Translate(glm::normalize(glm::cross(CameraComponent.Front, CameraComponent.Up)) * CameraSpeed);
			}
			if (Instance.IsKeyDown(GLFW_KEY_SPACE))
			{
				TransformComponent.Translate(CameraComponent.Up * CameraSpeed);
			}
			if (Instance.IsKeyDown(GLFW_KEY_E))
			{
				TransformComponent.Translate(glm::normalize(glm::cross(glm::cross(CameraComponent.Front, CameraComponent.Up), CameraComponent.Front)) * CameraSpeed);
			}
			if (Instance.IsKeyDown(GLFW_KEY_Q))
			{
				TransformComponent.Translate(glm::normalize(glm::cross(glm::cross(CameraComponent.Front, -CameraComponent.Up), CameraComponent.Front)) * CameraSpeed);
			}
#endif

			glm::vec2 MousePosition = Instance.GetMousePosition();
			if (MousePosition == glm::vec2(0, 0))
			{
				continue;
			}
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