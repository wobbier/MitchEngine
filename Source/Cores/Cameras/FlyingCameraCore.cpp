#include "PCH.h"
#include "FlyingCameraCore.h"
#include "Components/Cameras/FlyingCamera.h"
#include "Components/Transform.h"
#include "Components/Camera.h"
#include "Engine/Input.h"
#include "Events/EventManager.h"
#include "optick.h"
#include "Math/Vector3.h"

FlyingCameraCore::FlyingCameraCore() : Base(ComponentFilter().Requires<FlyingCamera>().Requires<Camera>())
{
}

FlyingCameraCore::~FlyingCameraCore()
{
}

void FlyingCameraCore::Init()
{
	std::vector<TypeId> events;
	events.push_back(TestEvent::GetEventId());
	EventManager::GetInstance().RegisterReceiver(this, events);
}

void FlyingCameraCore::Update(float dt)
{
	OPTICK_CATEGORY("FlyingCameraCore::Update", Optick::Category::Camera);

	Input& Instance = Input::GetInstance();
	if (Instance.IsKeyDown(KeyCode::Enter))
	{
		TestEvent testEvent;
		testEvent.Enabled = !InputEnabled;
		testEvent.Fire();
	}
	if (!InputEnabled)
	{
		//return;
	}

	auto Animatables = GetEntities();
	for (auto& InEntity : Animatables)
	{
		// Get our components that our Core required
		Transform& TransformComponent = InEntity.GetComponent<Transform>();
		FlyingCamera& FlyingCameraComponent = InEntity.GetComponent<FlyingCamera>();
		Camera& CameraComponent = InEntity.GetComponent<Camera>();

		if (Cam == &CameraComponent)
		{
			if (Instance.IsKeyDown(KeyCode::RightButton))
			{
				if (!PreviousMouseDown)
				{
					FirstUpdate = true;
					PreviousMouseDown = true;
				}
			}
			else
			{
				PreviousMouseDown = false;
				return;
			}
			float CameraSpeed = FlyingCameraComponent.FlyingSpeed;
			if (Instance.IsKeyDown(KeyCode::LeftShift))
			{
				CameraSpeed += FlyingCameraComponent.SpeedModifier;
			}
			CameraSpeed *= dt;
			if (Instance.IsKeyDown(KeyCode::W))
			{
				TransformComponent.SetPosition((CameraComponent.Front * CameraSpeed) + TransformComponent.GetPosition());
			}
			if (Instance.IsKeyDown(KeyCode::S))
			{
				TransformComponent.SetPosition(TransformComponent.GetPosition() - (CameraComponent.Front * CameraSpeed));
			}
			if (Instance.IsKeyDown(KeyCode::A))
			{
				TransformComponent.Translate(CameraComponent.Up.Cross(CameraComponent.Front.GetInternalVec()).Normalized() * CameraSpeed);
			}
			if (Instance.IsKeyDown(KeyCode::D))
			{
				TransformComponent.Translate(CameraComponent.Front.Cross(CameraComponent.Up.GetInternalVec()).Normalized() * CameraSpeed);
			}
			if (Instance.IsKeyDown(KeyCode::Space))
			{
				TransformComponent.Translate(CameraComponent.Up * CameraSpeed);
			}
			if (Instance.IsKeyDown(KeyCode::E))
			{
				TransformComponent.Translate(CameraComponent.Front.Cross(CameraComponent.Up).Cross(CameraComponent.Front).Normalized() * CameraSpeed);
			}
			if (Instance.IsKeyDown(KeyCode::Q))
			{
				TransformComponent.Translate(CameraComponent.Front.Cross(-CameraComponent.Up).Cross(CameraComponent.Front).Normalized() * CameraSpeed);
			}

			Vector2 MousePosition = Instance.GetMousePosition();
			if (MousePosition == Vector2(0, 0))
			{
				continue;
			}
			if (FirstUpdate)
			{
				LastX = MousePosition.X();
				LastY = MousePosition.Y();
				FirstUpdate = false;
			}

			float XOffset = MousePosition.X() - LastX;
			float YOffest = LastY - MousePosition.Y();
			LastX = MousePosition.X();
			LastY = MousePosition.Y();

			XOffset *= FlyingCameraComponent.LookSensitivity;
			YOffest *= FlyingCameraComponent.LookSensitivity;

			const float Yaw = CameraComponent.Yaw += XOffset;
			float Pitch = CameraComponent.Pitch += YOffest;
			
			if (Pitch > 89.0f)
				Pitch = 89.0f;
			if (Pitch < -89.0f)
				Pitch = -89.0f;

			Vector3 Front;
			Front.SetX(cos(glm::radians(Yaw)) * cos(glm::radians(Pitch)));
			Front.SetY(sin(glm::radians(Pitch)));
			Front.SetZ(sin(glm::radians(Yaw)) * cos(glm::radians(Pitch)));
			CameraComponent.Front = glm::normalize(Front.GetInternalVec());
			return;
		}
	}
}

bool FlyingCameraCore::OnEvent(const BaseEvent& evt)
{
	if (evt.GetEventId() == TestEvent::GetEventId())
	{
		const TestEvent& test = static_cast<const TestEvent&>(evt);
		InputEnabled = test.Enabled;
		return true;
	}

	return false;
}

#if ME_EDITOR

void FlyingCameraCore::OnEditorInspect()
{
	Base::OnEditorInspect();
}

#endif