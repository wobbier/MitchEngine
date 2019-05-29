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
#include "FilePath.h"
#include "Cores/EditorCore.h"
#include "Engine/Engine.h"
#include "Havana.h"
#include "Cores/SceneGraph.h"
#include "HavanaEvents.h"

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
			
			GetEngine().LoadScene("Assets/Alley.lvl");
		});

	EditorSceneManager->Update(DeltaTime, GetEngine().SceneNodes->RootTransform);

	Editor->UpdateWorld(GetEngine().GetWorld().lock().get(), GetEngine().SceneNodes->RootTransform, EditorSceneManager->GetEntities());

	Vector2 MainOutputSize = Editor->GetGameOutputSize();
	GetEngine().MainCamera = { Camera::CurrentCamera->Position, Camera::CurrentCamera->Front, Camera::CurrentCamera->Up, MainOutputSize, Camera::CurrentCamera->GetFOV() };
	GetEngine().EditorCamera = { Camera::EditorCamera->Position, Camera::EditorCamera->Front, Camera::EditorCamera->Up, Editor->WorldViewRenderSize, Camera::EditorCamera->GetFOV() };

	if (IsGameRunning())
	{
		mGame->OnUpdate(DeltaTime);
	}
}

void EditorApp::OnEnd()
{

	mGame->OnEnd();

	//destroy(mGame);
}

void EditorApp::OnInitialize()
{
	if (!Editor)
	{
		Editor = std::make_unique<Havana>(&GetEngine(), this, &GetEngine().GetRenderer());
		EditorSceneManager = new EditorCore(Editor.get());
	}
	GetEngine().GetWorld().lock()->AddCore<EditorCore>(*EditorSceneManager);
	GetEngine().LoadScene("Assets/Alley.lvl");

	mGame->OnInitialize();
}

void EditorApp::PostRender()
{
	Editor->Render(GetEngine().EditorCamera);
}

void EditorApp::StartGame()
{
	if (!m_isGameRunning)
	{
		m_isGameRunning = true;
		mGame->OnStart();
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
		GetEngine().GetWorld().lock()->Cleanup();
		GetEngine().InitGame();
		GetEngine().GetWorld().lock()->Simulate();

		return true;
	}

	return false;
}

void EditorApp::LoadGameDLL()
{
	HMODULE mod = LoadLibrary(L"Game.dll");
	if (mod)
	{
		typedef Game* (*CreateGameFunc)(const char*, Engine*);
		typedef void (*DestroyGameFunc)(Game*);
		CreateGameFunc create = (CreateGameFunc)GetProcAddress(mod, "CreateGame");
		DestroyGameFunc destroy = (DestroyGameFunc)GetProcAddress(mod, "DestroyGame");

		if (!create || !destroy)
		{
			std::cout << "Couldn't find" << std::endl;
			return;
		}
		mGame = create("test", &GetEngine());

		if (!mGame)
		{
			std::cout << "Game failed to create" << std::endl;
		}
	}
}
