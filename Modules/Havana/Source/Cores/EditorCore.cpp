#include "Cores/EditorCore.h"
#include "Components/Transform.h"
#include "Engine/World.h"
#include <stack>
#include "Havana.h"
#include "HavanaEvents.h"
#include "File.h"
#include "Components/Camera.h"
#include "Components/Cameras/FlyingCamera.h"
#include "Components/Graphics/Model.h"
#include "Engine/Engine.h"
#include "Path.h"
#include "nlohmann/json.hpp"
#include "ECS/Core.h"
#include "World/Scene.h"
#include "Components/Audio/AudioSource.h"
#include "Mathf.h"
#include "optick.h"

EditorCore::EditorCore(Havana* editor)
	: Base(ComponentFilter().Excludes<Transform>())
	, m_editor(editor)
{
	IsSerializable = false;
	std::vector<TypeId> events;
	events.push_back(SaveSceneEvent::GetEventId());
	events.push_back(NewSceneEvent::GetEventId());
	EventManager::GetInstance().RegisterReceiver(this, events);
	gizmo = new TranslationGizmo(m_editor);
}

EditorCore::~EditorCore()
{
}

void EditorCore::Init()
{
	EditorCameraTransform = new Transform();
	EditorCamera = new Camera();
	Camera::EditorCamera = EditorCamera;

	testAudio = new AudioSource("Assets/Sounds/CONSTITUTION.wav");
	//TransformEntity = GetWorld().CreateEntity();
	//TransformEntity.lock()->AddComponent<Transform>();
	//TransformEntity.lock()->AddComponent<Model>("Assets/Models/TransformGizmo.fbx");
}

void EditorCore::Update(float dt)
{
	OPTICK_CATEGORY("FlyingCameraCore::Update", Optick::Category::Camera);
	auto Keyboard = Input::GetInstance().GetKeyboardState();
	auto Mouse = Input::GetInstance().GetMouseState();
	if (Keyboard.M)
	{
		if (testAudio)
		{
			// Sound file

			// Goes in the component
			testAudio->Play(false);
		}
	}

	auto Animatables = GetEntities();
	if (m_editor->IsWorldViewFocused())
	{
		if (Keyboard.F)
		{
			IsFocusingTransform = true;

			// Keep a note of the time the movement started.
			startTime = 0.f;

			// Calculate the journey length.
			TravelDistance = (EditorCameraTransform->GetPosition() - Vector3()).Length();

			totalTime = 0.f;
		}

		totalTime += dt;
		
		if (!IsFocusingTransform)
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

			float CameraSpeed = FlyingSpeed;
			if (Keyboard.LeftShift)
			{
				CameraSpeed += SpeedModifier;
			}
			CameraSpeed *= dt;
			if (Keyboard.W)
			{
				EditorCameraTransform->SetPosition((EditorCamera->Front * CameraSpeed) + EditorCameraTransform->GetPosition());
			}
			if (Keyboard.S)
			{
				EditorCameraTransform->SetPosition(EditorCameraTransform->GetPosition() - (EditorCamera->Front * CameraSpeed));
			}
			if (Keyboard.A)
			{
				EditorCameraTransform->Translate(EditorCamera->Up.Cross(EditorCamera->Front.GetInternalVec()).Normalized() * CameraSpeed);
			}
			if (Keyboard.D)
			{
				EditorCameraTransform->Translate(EditorCamera->Front.Cross(EditorCamera->Up.GetInternalVec()).Normalized() * CameraSpeed);
			}
			if (Keyboard.Space)
			{
				EditorCameraTransform->Translate(EditorCamera->Up * CameraSpeed);
			}
			if (Keyboard.E)
			{
				EditorCameraTransform->Translate(EditorCamera->Front.Cross(EditorCamera->Up).Cross(EditorCamera->Front).Normalized() * CameraSpeed);
			}
			if (Keyboard.Q)
			{
				EditorCameraTransform->Translate(EditorCamera->Front.Cross(-EditorCamera->Up).Cross(EditorCamera->Front).Normalized() * CameraSpeed);
			}

			Vector2 MousePosition = Input::GetInstance().GetMousePosition();
			if (MousePosition == Vector2(0, 0))
			{
				return;
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

			XOffset *= LookSensitivity;
			YOffest *= LookSensitivity;

			const float Yaw = EditorCamera->Yaw += XOffset;
			float Pitch = EditorCamera->Pitch += YOffest;

			if (Pitch > 89.0f)
				Pitch = 89.0f;
			if (Pitch < -89.0f)
				Pitch = -89.0f;

			Vector3 Front;
			Front.SetX(cos(Mathf::Radians(Yaw)) * cos(Mathf::Radians(Pitch)));
			Front.SetY(sin(Mathf::Radians(Pitch)));
			Front.SetZ(sin(Mathf::Radians(Yaw)) * cos(Mathf::Radians(Pitch)));
			EditorCamera->Front = Front.Normalized();

		}
		else
		{
			// Distance moved = time * speed.
			float distCovered = (totalTime - startTime) * FocusSpeed;

			// Fraction of journey completed = current distance divided by total distance.
			float fracJourney = distCovered / TravelDistance;

			if (fracJourney <= .8f)
			{
				Vector3 lerp = Mathf::Lerp(EditorCameraTransform->GetPosition(), Vector3(), fracJourney);
				EditorCameraTransform->SetPosition(lerp);
			}
			else
			{
				IsFocusingTransform = false;
			}
		}

		EditorCamera->UpdateCameraTransform(EditorCameraTransform->GetPosition());
		return;
	}
}

void EditorCore::Update(float dt, Transform* rootTransform)
{
	OPTICK_EVENT("SceneGraph::Update");
	Update(dt);
	RootTransform = rootTransform;
	if (TryingToSaveNewScene)
	{
		ImGui::OpenPopup("SaveNewScenePopup");
		if (ImGui::BeginPopup("SaveNewScenePopup"))
		{
			char output[256] = "";
			if (ImGui::InputText("Destination", output, IM_ARRAYSIZE(output), ImGuiInputTextFlags_EnterReturnsTrue))
			{
				GetEngine().CurrentScene->Save(output, rootTransform);
				TryingToSaveNewScene = false;
			}
			ImGui::EndPopup();
		}
	}
	//gizmo->Update(m_editor->SelectedTransform, Camera::EditorCamera);
	//if (m_editor->SelectedTransform)
	//{
	//	if (TransformEntity.lock())
	//	{
	//		auto& trans = TransformEntity.lock()->GetComponent<Transform>();
	//		trans.SetPosition(m_editor->SelectedTransform->GetPosition());
	//	}
	//}
}

bool EditorCore::OnEvent(const BaseEvent& evt)
{
	if (evt.GetEventId() == SaveSceneEvent::GetEventId())
	{
		if (GetEngine().CurrentScene->IsNewScene())
		{
			TryingToSaveNewScene = true;
		}
		else
		{
			GetEngine().CurrentScene->Save(GetEngine().CurrentScene->FilePath.LocalPath, RootTransform);
		}
		return true;
	}

	return false;
}

void EditorCore::OnEntityAdded(Entity& NewEntity)
{
	Base::OnEntityAdded(NewEntity);

	//Transform& NewEntityTransform = NewEntity.GetComponent<Transform>();

	//if (NewEntityTransform.ParentTransform == nullptr && NewEntity != *RootEntity.lock().get())
	{
		//NewEntityTransform.SetParent(RootEntity.lock()->GetComponent<Transform>());
	}
}

#if ME_EDITOR

void EditorCore::OnEditorInspect()
{
	BaseCore::OnEditorInspect();
	ImGui::Text("Camera Settings");
	ImGui::DragFloat("Flying Speed", &FlyingSpeed);
	ImGui::DragFloat("Speed Modifier", &SpeedModifier);
	ImGui::DragFloat("Focus Speed", &FocusSpeed);
	ImGui::DragFloat("Look Sensitivity", &LookSensitivity, 0.01f);

	EditorCameraTransform->OnEditorInspect();

	EditorCamera->OnEditorInspect();
}
//
//void EditorCore::SaveWorld(const std::string & path)
//{
//	//if (GetEngine().CurrentScene->IsNewScene())
//	{
//		//TryingToSaveNewScene = true;
//
//		File worldFile(FilePath(path));
//		json world;
//
//		if (RootTransform->Children.size() > 0)
//		{
//			for (Transform* Child : RootTransform->Children)
//			{
//				SaveSceneRecursively(world["Scene"], Child);
//			}
//		}
//
//		json& cores = world["Cores"];
//		for (auto& core : GetWorld().GetAllCores())
//		{
//			json coreDef;
//			coreDef["Type"] = (*core).GetName();
//			cores.push_back(coreDef);
//		}
//
//		worldFile.Write(world.dump(4));
//		std::cout << world.dump(4) << std::endl;
//	}
//}

#endif