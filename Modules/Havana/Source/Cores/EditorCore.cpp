#include <Cores/EditorCore.h>

#include <Components/Camera.h>
#include <Components/Graphics/Mesh.h>
#include <Components/Graphics/Model.h>
#include <Components/Transform.h>
#include <Cores/Rendering/RenderCore.h>
#include <Engine/Engine.h>
#include <Events/SceneEvents.h>
#include <Havana.h>
#include <HavanaEvents.h>
#include <Mathf.h>
#include <optick.h>
#include <Window/IWindow.h>
#include <World/Scene.h>
#include <Utils/EditorConfig.h>

#if ME_EDITOR

EditorCore::EditorCore(Havana* editor)
	: Base(ComponentFilter().Excludes<Transform>())
	, m_editor(editor)
{
	SetIsSerializable(false);

	EditorCameraTransform = std::make_shared<Transform>();
	EditorCamera = new Camera();

	std::vector<TypeId> events;
	events.push_back(SaveSceneEvent::GetEventId());
	events.push_back(NewSceneEvent::GetEventId());
	//events.push_back(Moonlight::PickingEvent::GetEventId());
	EventManager::GetInstance().RegisterReceiver(this, events);
}

EditorCore::~EditorCore()
{
}

void EditorCore::Init()
{
	Camera::EditorCamera = EditorCamera;

	EditorCameraTransform->SetWorldPosition(EditorConfig::GetInstance().CameraPosition);
	EditorCameraTransform->SetRotation(EditorConfig::GetInstance().CameraRotation);
}

void EditorCore::Update(float dt)
{
	OPTICK_CATEGORY("FlyingCameraCore::Update", Optick::Category::Camera);

	Input& input = m_editor->GetInput();

	if (m_editor->IsWorldViewFocused() || FirstUpdate)
	{
		if (input.IsKeyDown(KeyCode::F))
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
			if (!input.IsMouseButtonDown(MouseButton::Right))
			{
				return;
			}

			float CameraSpeed = m_flyingSpeed;
			if (input.IsKeyDown(KeyCode::LeftShift))
			{
				CameraSpeed += m_speedModifier;
			}
			CameraSpeed *= dt;

			const Vector3& Front = EditorCameraTransform->Front();

			if (input.IsKeyDown(KeyCode::W))
			{
				EditorCameraTransform->Translate(Front * CameraSpeed);
			}
			if (input.IsKeyDown(KeyCode::S))
			{
				EditorCameraTransform->Translate((Front * CameraSpeed) * -1.f);
			}
			if (input.IsKeyDown(KeyCode::A))
			{
				EditorCameraTransform->Translate(-EditorCameraTransform->Right() * CameraSpeed);
			}
			if (input.IsKeyDown(KeyCode::D))
			{
				EditorCameraTransform->Translate(EditorCameraTransform->Right() * CameraSpeed);
			}
			if (input.IsKeyDown(KeyCode::Space))
			{
				EditorCameraTransform->Translate(Vector3::Up * CameraSpeed);
			}
			if (input.IsKeyDown(KeyCode::E))
			{
				EditorCameraTransform->Translate(EditorCameraTransform->Up() * CameraSpeed);
			}
			if (input.IsKeyDown(KeyCode::Q))
			{
				EditorCameraTransform->Translate(-EditorCameraTransform->Up() * CameraSpeed);
			}

			//Vector2 MousePosition = m_editor->GetInput().GetMousePosition();
// 			if (MousePosition.IsZero())
// 			{
// 				return;
// 			}

// 			if (FirstUpdate)
// 			{
// 				LastX = MousePosition.x;
// 				LastY = MousePosition.y;
// 				FirstUpdate = false;
// 			}

			Vector2 currentState = input.GetRelativeMousePosition();
			float XOffset = ((currentState.x) * m_lookSensitivity) * dt;
			float YOffest = ((currentState.y) * m_lookSensitivity) * dt;

			const float Yaw = EditorCamera->Yaw -= XOffset;
			float Pitch = EditorCamera->Pitch + YOffest;

			if (Pitch > 89.0f)
			{
				Pitch = 89.0f;
			}
			if (Pitch < -89.0f)
			{
				Pitch = -89.0f;
			}
			EditorCamera->Pitch = Pitch;
			//Vector3 newFront;
			//newFront.x = (cos(Mathf::Radians(Yaw)) * cos(Mathf::Radians(Pitch)));
			//newFront.y = (sin(Mathf::Radians(Pitch)));
			//newFront.z = (sin(Mathf::Radians(Yaw)) * cos(Mathf::Radians(Pitch)));
			//Quaternion q(newFront);
			//q.SetLookRotation(newFront.Normalized());
			//EditorCameraTransform->SetRotation(q);
			EditorCameraTransform->SetRotation(Vector3(Pitch, -Yaw, 0.0f));
			//EditorCameraTransform->LookAt(newFront.Normalized());
			EditorConfig::GetInstance().CameraPosition = Vector3(EditorCameraTransform->GetPosition());
			EditorConfig::GetInstance().CameraRotation = Vector3(EditorCameraTransform->GetRotationEuler());
		}
		else
		{
			// Distance moved = time * speed.
			float distCovered = (totalTime - startTime) * m_focusSpeed;

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
	}
}

void EditorCore::Update(float dt, Transform* rootTransform)
{
	OPTICK_EVENT("EditorCore::Update");

	RootTransform = rootTransform;

	Update(dt);
}

bool EditorCore::OnEvent(const BaseEvent& evt)
{
	if (evt.GetEventId() == SaveSceneEvent::GetEventId())
	{
		const SaveSceneEvent& event = static_cast<const SaveSceneEvent&>(evt);
		if (GetEngine().CurrentScene->IsNewScene() || event.SaveAs)
		{
			RequestAssetSelectionEvent evt([this](const Path& inPath) {
				GetEngine().CurrentScene->Save(inPath.LocalPath, RootTransform);
				GetEngine().GetConfig().SetValue(std::string("CurrentScene"), inPath.LocalPath);
				GetEngine().GetConfig().Save();
				}, AssetType::Level, true);
			evt.Fire();
		}
		else
		{
			GetEngine().CurrentScene->Save(GetEngine().CurrentScene->FilePath.LocalPath, RootTransform);
			GetEngine().GetConfig().SetValue(std::string("CurrentScene"), GetEngine().CurrentScene->FilePath.LocalPath);
			GetEngine().GetConfig().Save();
		}
		return true;
	}
	/*else if (evt.GetEventId() == Moonlight::PickingEvent::GetEventId())
	{
		const Moonlight::PickingEvent& casted = static_cast<const Moonlight::PickingEvent&>(evt);

		auto ents = GetEngine().GetWorld().lock()->GetCore(RenderCore::GetTypeId())->GetEntities();
		for (auto& ent : ents)
		{
			if (!ent.HasComponent<Mesh>())
			{
				continue;
			}
			if (ent.GetComponent<Mesh>().Id == casted.Id)
			{
				Transform* meshTransform = &ent.GetComponent<Transform>();
				std::stack<Transform*> parentEnts;
				parentEnts.push(meshTransform->GetParent());

				static Transform* selectedParentObjec = nullptr;

				while (parentEnts.size() > 0)
				{
					Transform* parent = parentEnts.top();
					parentEnts.pop();
					if (!parent)
					{
						continue;
					}

					if (parent->Parent->HasComponent<Model>())
					{
						Transform* selectedModel = &parent->Parent->GetComponent<Transform>();
						if (m_editor->SelectedTransform == nullptr || selectedParentObjec != selectedModel)
						{
							m_editor->SelectedTransform = selectedModel;
							selectedParentObjec = selectedModel;
							break;
						}

						if (meshTransform != m_editor->SelectedTransform)
						{
							m_editor->SelectedTransform = meshTransform;
						}
						else
						{
							m_editor->SelectedTransform = selectedModel;
						}
						break;
					}
					else
					{
						parentEnts.push(parent->GetParent());
					}
				}
			}
		}
		return true;
	}*/

	return false;
}

void EditorCore::OnEntityAdded(Entity& NewEntity)
{
	Base::OnEntityAdded(NewEntity);
}

Havana* EditorCore::GetEditor() const
{
	return m_editor;
}

SharedPtr<Transform> EditorCore::GetEditorCameraTransform() const
{
	return EditorCameraTransform;
}

void EditorCore::OnEditorInspect()
{
	BaseCore::OnEditorInspect();

	ImGui::Text("Camera Settings");
	ImGui::DragFloat("Flying Speed", &m_flyingSpeed);
	ImGui::DragFloat("Speed Modifier", &m_speedModifier);
	ImGui::DragFloat("Focus Speed", &m_focusSpeed);
	ImGui::DragFloat("Look Sensitivity", &m_lookSensitivity, 0.01f);

	EditorCameraTransform->OnEditorInspect();

	EditorCamera->OnEditorInspect();
}

#endif
