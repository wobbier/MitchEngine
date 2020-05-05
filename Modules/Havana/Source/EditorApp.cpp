#include "EditorApp.h"
#include "ECS/Component.h"
#include "Engine/Clock.h"
#include "Components/Transform.h"
#include "ECS/Entity.h"
#include <string>
#include "Engine/Input.h"
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
#include "Events/SceneEvents.h"
#include "RenderCommands.h"
#include "Events/Event.h"
#include <SimpleMath.h>
#include "Math/Matirx4.h"
#include "Math/Frustrum.h"

EditorApp::EditorApp()
{
	std::vector<TypeId> events;
	events.push_back(NewSceneEvent::GetEventId());
	events.push_back(SceneLoadedEvent::GetEventId());
	EventManager::GetInstance().RegisterReceiver(this, events);

}

EditorApp::~EditorApp()
{
}

void EditorApp::OnStart()
{
}

void EditorApp::OnUpdate(float DeltaTime)
{
	Editor->NewFrame([this]() {
		StartGame();
		m_isGamePaused = false;
		m_isGameRunning = true;
		}
		, [this]() {
			m_isGamePaused = true;
		}
			, [this]() {
			StopGame();
			m_isGameRunning = false;
			m_isGamePaused = false;
			Editor->ClearSelection();
			//GetEngine().LoadScene("Assets/Alley.lvl");
		});

	EditorSceneManager->Update(DeltaTime, GetEngine().SceneNodes->GetRootTransform());

	Editor->UpdateWorld(GetEngine().GetWorld().lock().get(), GetEngine().SceneNodes->GetRootTransform(), EditorSceneManager->GetEntities());

	UpdateCameras();
}

void EditorApp::UpdateCameras()
{
	Vector2 MainOutputSize = Editor->GetGameOutputSize();

	if (!Camera::CurrentCamera)
	{
		Camera::CurrentCamera = Camera::EditorCamera;
	}

	Moonlight::CameraData EditorCamera;

	Camera::CurrentCamera->OutputSize = MainOutputSize;
	EditorCamera.Position = EditorSceneManager->GetEditorCameraTransform()->GetWorldPosition();
	EditorCamera.Front = EditorSceneManager->GetEditorCameraTransform()->Front();
	EditorCamera.Up = Vector3::Up;
	EditorCamera.OutputSize = Editor->WorldViewRenderSize;
	EditorCamera.FOV = Camera::EditorCamera->GetFOV();
	EditorCamera.Near = Camera::EditorCamera->Near;
	EditorCamera.Far = Camera::EditorCamera->Far;
	EditorCamera.Skybox = Camera::CurrentCamera->Skybox;
	EditorCamera.ClearColor = Camera::CurrentCamera->ClearColor;
	EditorCamera.ClearType = Camera::CurrentCamera->ClearType;
	EditorCamera.Projection = Camera::EditorCamera->Projection;
	EditorCamera.OrthographicSize = Camera::EditorCamera->OrthographicSize;
	EditorCamera.CameraFrustum = Camera::EditorCamera->CameraFrustum;

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
		InitialLevel = GetEngine().GetConfig().GetValue("CurrentScene");
		Editor = std::make_unique<Havana>(&GetEngine(), this, &GetEngine().GetRenderer());
		EditorSceneManager = new EditorCore(Editor.get());
		NewSceneEvent evt;
		evt.Fire();
		GetEngine().GetWorld().lock()->AddCore<EditorCore>(*EditorSceneManager);
		GetEngine().LoadScene(InitialLevel);
	}
	else
	{
		GetEngine().GetWorld().lock()->AddCore<EditorCore>(*EditorSceneManager);
	}
}

void EditorApp::PostRender()
{
	Editor->Render(GetEngine().EditorCamera);
}

void EditorApp::StartGame()
{
	if (!m_isGameRunning)
	{
		GetEngine().GetWorld().lock()->Start();
		m_isGameRunning = true;
	}
}

void EditorApp::StopGame()
{
	if (m_isGameRunning)
	{
		if (GetEngine().GetWorld().lock())
		{
			GetEngine().GetWorld().lock()->Destroy();
		}
		NewSceneEvent evt;
		evt.Fire();
		GetEngine().LoadScene(InitialLevel);
		GetEngine().GetWorld().lock()->Stop();
		m_isGameRunning = false;
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
		GetEngine().InitGame();
		GetEngine().GetWorld().lock()->Simulate();
	}
	else if (evt.GetEventId() == SceneLoadedEvent::GetEventId())
	{
		const SceneLoadedEvent& test = static_cast<const SceneLoadedEvent&>(evt);

		Editor->SetWindowTitle("Havana - " + test.LoadedScene->FilePath.LocalPath);
		if (m_isGameRunning)
		{
			GetEngine().GetWorld().lock()->Start();
		}
	}

	return false;
}