#include "PCH.h"
#include "FlyingCameraCore.h"
#include "Components/Cameras/FlyingCamera.h"
#include "Components/Transform.h"
#include "Components/Camera.h"
#include "Engine/Input.h"
#include "Events/EventManager.h"
#include "optick.h"
#include "Math/Vector3.h"
#include "Mathf.h"
#include "Engine/Engine.h"

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

	auto Keyboard = GetEngine().GetInput().GetKeyboardState();
	auto Mouse = GetEngine().GetInput().GetMouseState();
	if (Keyboard.Enter)
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
			if (Mouse.rightButton)
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
			if (Keyboard.LeftShift)
			{
				CameraSpeed += FlyingCameraComponent.SpeedModifier;
			}
			CameraSpeed *= dt;
			if (Keyboard.W)
			{
				TransformComponent.SetPosition((CameraComponent.Front * CameraSpeed) + TransformComponent.GetPosition());
			}
			if (Keyboard.S)
			{
				TransformComponent.SetPosition(TransformComponent.GetPosition() - (CameraComponent.Front * CameraSpeed));
			}
			if (Keyboard.A)
			{
				TransformComponent.Translate(Vector3::Up.Cross(CameraComponent.Front.GetInternalVec()).Normalized() * CameraSpeed);
			}
			if (Keyboard.D)
			{
				TransformComponent.Translate(CameraComponent.Front.Cross(Vector3::Up.GetInternalVec()).Normalized() * CameraSpeed);
			}
			if (Keyboard.Space)
			{
				TransformComponent.Translate(Vector3::Up * CameraSpeed);
			}
			if (Keyboard.E)
			{
				TransformComponent.Translate(CameraComponent.Front.Cross(Vector3::Up).Cross(CameraComponent.Front).Normalized() * CameraSpeed);
			}
			if (Keyboard.Q)
			{
				TransformComponent.Translate(CameraComponent.Front.Cross(-Vector3::Up).Cross(CameraComponent.Front).Normalized() * CameraSpeed);
			}

			Vector2 MousePosition = GetEngine().GetInput().GetMousePosition();
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
			Front.SetX(cos(Mathf::Radians(Yaw)) * cos(Mathf::Radians(Pitch)));
			Front.SetY(sin(Mathf::Radians(Pitch)));
			Front.SetZ(sin(Mathf::Radians(Yaw)) * cos(Mathf::Radians(Pitch)));
			CameraComponent.Front = Front.Normalized();
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