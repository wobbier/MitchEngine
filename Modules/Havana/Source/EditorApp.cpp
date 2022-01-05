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
#include "Cores/SceneCore.h"
#include "Events/SceneEvents.h"
#include "RenderCommands.h"
#include "Events/Event.h"
#include "Math/Matrix4.h"
#include "Math/Frustrum.h"
#include "optick.h"
#include <Core/JobQueueOld.h>
#include <Math/Quaternion.h>
#include "HavanaEvents.h"
#include <Utils/EditorConfig.h>

#if ME_EDITOR

EditorApp::EditorApp(int argc, char** argv)
	: Game(argc, argv)
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
	OPTICK_CATEGORY("EditorApp::OnUpdate", Optick::Category::GameLogic);

	Editor->NewFrame();
	Transform* root = GetEngine().SceneNodes->GetRootTransform();

	EditorSceneManager->Update(DeltaTime, root);
	Editor->UpdateWorld(root, EditorSceneManager->GetEntities());

	UpdateCameras();
}

void EditorApp::UpdateCameras()
{
	if (!Camera::CurrentCamera)
	{
		Camera::CurrentCamera = Camera::EditorCamera;
	}

	Moonlight::CameraData& EditorCamera = GetEngine().EditorCamera;

	EditorCamera.Position = EditorSceneManager->GetEditorCameraTransform()->GetPosition();
	EditorCamera.Front = EditorSceneManager->GetEditorCameraTransform()->Front();
	EditorCamera.Up = EditorSceneManager->GetEditorCameraTransform()->Up();
	EditorCamera.OutputSize = Editor->GetWorldEditorRenderSize();
	EditorCamera.FOV = Camera::EditorCamera->GetFOV();
	EditorCamera.Near = Camera::EditorCamera->Near;
	EditorCamera.Far = Camera::EditorCamera->Far;
	EditorCamera.Skybox = Camera::CurrentCamera->Skybox;
	EditorCamera.ClearColor = Camera::CurrentCamera->ClearColor;
	EditorCamera.ClearType = Camera::CurrentCamera->ClearType;
	EditorCamera.Projection = Camera::EditorCamera->Projection;
	EditorCamera.OrthographicSize = Camera::EditorCamera->OrthographicSize;
	//EditorCamera.CameraFrustum = Camera::EditorCamera->CameraFrustum;
	EditorCamera.View = EditorSceneManager->GetEditorCameraTransform()->GetWorldToLocalMatrix();
	GetEngine().EditorCamera = EditorCamera;
}

void EditorApp::OnEnd()
{
	Editor->Save();
	EditorConfig::GetInstance().Save();
}

void EditorApp::OnInitialize()
{
	if (!Editor)
	{
		EditorConfig::GetInstance().Init();
		EditorConfig::GetInstance().Load();
		InitialLevel = GetEngine().GetConfig().GetValue("CurrentScene");
		Editor = MakeUnique<Havana>(&GetEngine(), this);
		EditorSceneManager = new EditorCore(Editor.get());

		Editor->SetGameCallbacks([this]() {
			StartGame();
			m_isGamePaused = false;
			m_isGameRunning = true;
			//Editor->SetViewportMode(ViewportMode::Game);
		}
		, [this]() {
			m_isGamePaused = true;
		}
		, [this]() {
			m_isGamePaused = false;
			//Editor->SetViewportMode(ViewportMode::World);
			ClearInspectEvent evt;
			evt.Fire();
			StopGame();
			//GetEngine().LoadScene("Assets/Alley.lvl");
		});

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
	Editor->GetInput().PostUpdate();
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
		m_isGameRunning = false;
		NewSceneEvent evt;
		evt.Fire();
		InitialLevel = GetEngine().GetConfig().GetValue("CurrentScene");
		GetEngine().LoadScene(InitialLevel);
		GetEngine().GetWorld().lock()->Stop();
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

#endif