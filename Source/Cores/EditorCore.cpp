#include "PCH.h"
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

EditorCore::EditorCore(Havana* editor)
	: Base(ComponentFilter().Excludes<Transform>())
	, m_editor(editor)
{
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

	//TransformEntity = GetWorld().CreateEntity();
	//TransformEntity.lock()->AddComponent<Transform>();
	//TransformEntity.lock()->AddComponent<Model>("Assets/Models/TransformGizmo.fbx");
}

void EditorCore::Update(float dt)
{
	OPTICK_CATEGORY("FlyingCameraCore::Update", Optick::Category::Camera);

	Input& Instance = Input::GetInstance();
	auto Animatables = GetEntities();
	if (m_editor->IsWorldViewFocused())
	{
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
			float CameraSpeed = FlyingSpeed;
			if (Instance.IsKeyDown(KeyCode::LeftShift))
			{
				CameraSpeed += SpeedModifier;
			}
			CameraSpeed *= dt;
			if (Instance.IsKeyDown(KeyCode::W))
			{
				EditorCameraTransform->SetPosition((EditorCamera->Front * CameraSpeed) + EditorCameraTransform->GetPosition());
			}
			if (Instance.IsKeyDown(KeyCode::S))
			{
				EditorCameraTransform->SetPosition(EditorCameraTransform->GetPosition() - (EditorCamera->Front * CameraSpeed));
			}
			if (Instance.IsKeyDown(KeyCode::A))
			{
				EditorCameraTransform->Translate(EditorCamera->Up.Cross(EditorCamera->Front.GetInternalVec()).Normalized() * CameraSpeed);
			}
			if (Instance.IsKeyDown(KeyCode::D))
			{
				EditorCameraTransform->Translate(EditorCamera->Front.Cross(EditorCamera->Up.GetInternalVec()).Normalized() * CameraSpeed);
			}
			if (Instance.IsKeyDown(KeyCode::Space))
			{
				EditorCameraTransform->Translate(EditorCamera->Up * CameraSpeed);
			}
			if (Instance.IsKeyDown(KeyCode::E))
			{
				EditorCameraTransform->Translate(EditorCamera->Front.Cross(EditorCamera->Up).Cross(EditorCamera->Front).Normalized() * CameraSpeed);
			}
			if (Instance.IsKeyDown(KeyCode::Q))
			{
				EditorCameraTransform->Translate(EditorCamera->Front.Cross(-EditorCamera->Up).Cross(EditorCamera->Front).Normalized() * CameraSpeed);
			}

			Vector2 MousePosition = Instance.GetMousePosition();
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
			Front.SetX(cos(glm::radians(Yaw)) * cos(glm::radians(Pitch)));
			Front.SetY(sin(glm::radians(Pitch)));
			Front.SetZ(sin(glm::radians(Yaw)) * cos(glm::radians(Pitch)));
			EditorCamera->Front = glm::normalize(Front.GetInternalVec());

			EditorCamera->UpdateCameraTransform(EditorCameraTransform->GetPosition());

			return;
		}
	}
}

void EditorCore::Update(float dt, Transform * rootTransform)
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

bool EditorCore::OnEvent(const BaseEvent & evt)
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

void EditorCore::SaveSceneRecursively(json & d, Transform * CurrentTransform)
{
	OPTICK_EVENT("SceneGraph::UpdateRecursively");
	json thing;

	thing["Name"] = CurrentTransform->Name;

	json& componentsJson = thing["Components"];
	Entity* ent = GetWorld().GetEntity(CurrentTransform->Parent).lock().get();

	auto comps = ent->GetAllComponents();
	for (auto comp : comps)
	{
		json compJson;
		comp->Serialize(compJson);
		componentsJson.push_back(compJson);
	}
	if (CurrentTransform->Children.size() > 0)
	{
		for (Transform* Child : CurrentTransform->Children)
		{
			SaveSceneRecursively(thing["Children"], Child);
		}
	}
	d.push_back(thing);
}

void EditorCore::OnEntityAdded(Entity & NewEntity)
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
	ImGui::DragFloat("Look Sensitivity", &LookSensitivity, 0.01f);

	EditorCameraTransform->OnEditorInspect();
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