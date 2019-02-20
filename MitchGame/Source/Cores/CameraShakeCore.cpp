#include "CameraShakeCore.h"
#include "Components/CameraShake.h"
#include "Components/Transform.h"
#include "Components/Camera.h"
#include "Engine/Input.h"
#include "Events/EventManager.h"
#include "PerlinNoise.hpp"
#include "Engine/Clock.h"
#include "Brofiler.h"

CameraShakeCore::CameraShakeCore()
	: Base(ComponentFilter().Requires<CameraShake>().Requires<Camera>())
	, Noise(2001)
{
}

CameraShakeCore::~CameraShakeCore()
{
}

void CameraShakeCore::Init()
{
}

void CameraShakeCore::Update(float dt)
{
	BROFILER_CATEGORY("CameraShakeCore::Update", Brofiler::Color::Green);
	TotalTime += dt;
	Input& Instance = Input::GetInstance();

	auto Animatables = GetEntities();
	for (auto& InEntity : Animatables)
	{
		// Get our components that our Core required
		Transform& TransformComponent = InEntity.GetComponent<Transform>();
		CameraShake& CameraShakeComponent = InEntity.GetComponent<CameraShake>();
		Camera& CameraComponent = InEntity.GetComponent<Camera>();

		if (&CameraComponent == Camera::CurrentCamera)
		{
			float Yaw = CameraShakeComponent.MaxDirection.x * CameraShakeComponent.ShakeAmount * Noise.noise(TotalTime);
			float Pitch = CameraShakeComponent.MaxDirection.y * CameraShakeComponent.ShakeAmount * Noise.noise(TotalTime + 500);
			float Roll = CameraShakeComponent.MaxDirection.z * CameraShakeComponent.ShakeAmount * Noise.noise(TotalTime + 1000);
			Pitch += CameraComponent.Pitch;
			Yaw += CameraComponent.Yaw;
			CameraComponent.Up += CameraComponent.Roll;
			glm::vec3 Front;
			Front.x = cos(glm::radians(Yaw)) * cos(glm::radians(Pitch));
			Front.y = sin(glm::radians(Pitch));
			Front.z = sin(glm::radians(Yaw)) * cos(glm::radians(Pitch));
			CameraComponent.Front += glm::normalize(Front);
		}
	}
}