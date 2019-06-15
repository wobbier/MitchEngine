#include "EditorApp.h"
#include "ECS/Component.h"
#include "Engine/Clock.h"
#include "Components/Transform.h"
#include "ECS/Entity.h"
#include <string>
#include "Engine/Input.h"
#include "Components/Animation.h"
#include "Components/Camera.h"
#include "Components/Physics/Rigidbody.h"
#include "Components/Graphics/Model.h"
#include "Components/Lighting/Light.h"

#include <memory>
#include "Engine/World.h"
#include "Path.h"
#include "Cores/EditorCore.h"
#include "Engine/Engine.h"
#include "Havana.h"
#include "Cores/SceneGraph.h"
#include "HavanaEvents.h"
#include "RenderCommands.h"

EditorApp::EditorApp()
{
	std::vector<TypeId> events;
	events.push_back(NewSceneEvent::GetEventId());
	EventManager::GetInstance().RegisterReceiver(this, events);
}

EditorApp::~EditorApp()
{
}

void EditorApp::OnStart()
{
	UpdateCameras();
}

void EditorApp::OnUpdate(float DeltaTime)
{
	Editor->NewFrame([this]() {
		StartGame();
		m_isGamePaused = false;
		}
		, [this]() {
			m_isGamePaused = true;
		}
			, [this]() {
			m_isGameRunning = false;
			m_isGamePaused = false;
			Editor->ClearSelection();
			//GetEngine().LoadScene("Assets/Alley.lvl");
		});

	EditorSceneManager->Update(DeltaTime, GetEngine().SceneNodes->RootTransform);

	Editor->UpdateWorld(GetEngine().GetWorld().lock().get(), GetEngine().SceneNodes->RootTransform, EditorSceneManager->GetEntities());

	UpdateCameras();
}

void EditorApp::UpdateCameras()
{
	Vector2 MainOutputSize = Editor->GetGameOutputSize();

	Moonlight::CameraData MainCamera;
	MainCamera.Position = Camera::CurrentCamera->Position;
	MainCamera.Front = Camera::CurrentCamera->Front;
	MainCamera.Up = Camera::CurrentCamera->Up;
	MainCamera.OutputSize = MainOutputSize;
	MainCamera.FOV = Camera::CurrentCamera->GetFOV();
	MainCamera.Skybox = Camera::CurrentCamera->Skybox;
	GetEngine().MainCamera = MainCamera;

	Moonlight::CameraData EditorCamera;
	MainCamera.Position = Camera::EditorCamera->Position;
	MainCamera.Front = Camera::EditorCamera->Front;
	MainCamera.Up = Camera::EditorCamera->Up;
	MainCamera.OutputSize = Editor->WorldViewRenderSize;
	MainCamera.FOV = Camera::EditorCamera->GetFOV();
	GetEngine().EditorCamera = EditorCamera;
}

void EditorApp::OnEnd()
{

	//destroy(mGame);
}

void EditorApp::OnInitialize()
{
	if (!Editor)
	{
		Editor = std::make_unique<Havana>(&GetEngine(), this, &GetEngine().GetRenderer());
		EditorSceneManager = new EditorCore(Editor.get());
		NewSceneEvent evt;
		evt.Fire();
	}
	GetEngine().GetWorld().lock()->AddCore<EditorCore>(*EditorSceneManager);
	GetEngine().LoadScene("Assets/Test.lvl");
}

void EditorApp::PostRender()
{
	UpdateCameras();
	Editor->Render(GetEngine().EditorCamera);
}

void EditorApp::StartGame()
{
	if (!m_isGameRunning)
	{
		m_isGameRunning = true;
	}
}

const bool EditorApp::IsGameRunning() const
{
	return m_isGameRunning;
}

const bool EditorApp::IsGamePaused() const
{
	return m_isGamePaused;
}

bool EditorApp::OnEvent(const BaseEvent& evt)
{
	if (evt.GetEventId() == NewSceneEvent::GetEventId())
	{
		const NewSceneEvent& test = static_cast<const NewSceneEvent&>(evt);
		GetEngine().LoadScene("");
		GetEngine().GetWorld().lock()->Cleanup();
		GetEngine().InitGame();
		GetEngine().GetWorld().lock()->Simulate();

		return true;
	}

	return false;
}