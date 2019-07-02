#include "PCH.h"
#include "Engine.h"
#include "Logger.h"
#include "Config.h"
#include "Window/UWPWindow.h"
#include "Window/D3D12Window.h"
#include "Events/EventManager.h"
#include "Cores/PhysicsCore.h"
#include "Cores/Cameras/CameraCore.h"
#include "Cores/SceneGraph.h"
#include "Cores/Rendering/RenderCore.h"
#include "Game.h"
#include "Window/IWindow.h"
#include "Input.h"
#include "Havana.h"
#include "Components/Transform.h"
#include "Dementia.h"
#include "Cores/EditorCore.h"
#include "HavanaEvents.h"
#include "Components/Camera.h"
#include "Components/Cameras/FlyingCamera.h"
#include "Cores/Cameras/FlyingCameraCore.h"

Engine& GetEngine()
{
	return Engine::GetInstance();
}

Engine::Engine()
	: Running(true)
	, GameClock(Clock::GetInstance())
{
	std::vector<TypeId> events;
	events.push_back(LoadSceneEvent::GetEventId());
	EventManager::GetInstance().RegisterReceiver(this, events);
}

Engine::~Engine()
{

}

void Engine::Init(Game* game)
{
	if (m_isInitialized || !game)
	{
		return;
	}

	m_game = game;

	Logger::GetInstance().SetLogFile("Engine.txt");
	Logger::GetInstance().SetLogPriority(Logger::LogType::Info);
	Logger::GetInstance().Log(Logger::LogType::Info, "Starting the MitchEngine.");

	EngineConfig = new Config("Assets\\Config\\Engine.cfg");

	//auto WindowConfig = EngineConfig->Root["window"];
	int WindowWidth = 1920;//WindowConfig["width"].asInt();
	int WindowHeight = 1080;//WindowConfig["height"].asInt();

#if ME_PLATFORM_WIN64
	std::function<void(const Vector2&)> Func = [this](const Vector2 & NewSize)
	{
		if (m_renderer)
		{
			m_renderer->WindowResized(NewSize);
		}
	};
	GameWindow = new D3D12Window("MitchEngine", Func, WindowWidth, WindowHeight);
#endif
#if ME_PLATFORM_UWP
	GameWindow = new UWPWindow("MitchEngine", WindowWidth, WindowHeight);
#endif

	m_renderer = new Moonlight::Renderer();
	m_renderer->WindowResized(GameWindow->GetSize());

	GameWorld = std::make_shared<World>();

	Cameras = new CameraCore();

	SceneNodes = new SceneGraph();

	ModelRenderer = new RenderCore();

	m_renderer->Init();

	InitGame();

	m_isInitialized = true;
}

void Engine::InitGame()
{
	GameWorld->AddCore<CameraCore>(*Cameras);
	GameWorld->AddCore<SceneGraph>(*SceneNodes);
	GameWorld->AddCore<RenderCore>(*ModelRenderer);

	m_game->OnInitialize();
}

void Engine::StopGame()
{
	m_game->OnEnd();
}

void Engine::Run()
{
	m_game->OnStart();

	GameClock.Reset();
	// Game loop
	forever
	{
		OPTICK_FRAME("MainLoop")

		// Check and call events
		GameWindow->ParseMessageQueue();

		if (GameWindow->ShouldClose())
		{
			StopGame();
			break;
		}

		EventManager::GetInstance().FirePendingEvents();

		float time = GameClock.GetTimeInMilliseconds();
		const float deltaTime = GameClock.deltaTime = (time <= 0.0f || time >= 0.3) ? 0.0001f : time;

		GameWorld->Simulate();

		AccumulatedTime += deltaTime;

		// Update our engine

		{
			OPTICK_CATEGORY("MainLoop::GameUpdate", Optick::Category::GameLogic);
			m_game->OnUpdate(deltaTime);
		}

			SceneNodes->Update(deltaTime);

			Cameras->Update(deltaTime);

			ModelRenderer->Update(AccumulatedTime);

			AccumulatedTime -= 1.0f / FPS;

#if !ME_EDITOR
			Vector2 MainOutputSize = m_renderer->GetDevice().GetOutputSize();
			MainCamera = { Camera::CurrentCamera->Position, Camera::CurrentCamera->Front, Camera::CurrentCamera->Up, MainOutputSize, Camera::CurrentCamera->GetFOV() };
			EditorCamera = MainCamera;
#endif

			m_renderer->Render([this]() {
				m_game->PostRender();
			}, MainCamera, EditorCamera);

			Sleep(5);
	}
}

bool Engine::OnEvent(const BaseEvent& evt)
{
	if (evt.GetEventId() == LoadSceneEvent::GetEventId())
	{
		const LoadSceneEvent& test = static_cast<const LoadSceneEvent&>(evt);
		//InputEnabled = test.Enabled;
		LoadScene(test.Level);

		return true;
	}

	return false;
}

Moonlight::Renderer& Engine::GetRenderer() const
{
	return *m_renderer;
}

std::weak_ptr<World> Engine::GetWorld() const
{
	return GameWorld;
}

const bool Engine::IsInitialized() const
{
	return m_isInitialized;
}

bool Engine::IsRunning() const
{
	return true;
}

void Engine::Quit() { Running = false; }

IWindow* Engine::GetWindow()
{
	return GameWindow;
}

void Engine::LoadScene(const std::string& SceneFile)
{
	SceneNodes->Init();
	Cameras->Init();
	if (CurrentScene)
	{
		CurrentScene->UnLoad();
		delete CurrentScene;
		CurrentScene = nullptr;
	}

	GameWorld->Cleanup();
	CurrentScene = new Scene(SceneFile);
#if ME_EDITOR
	//Editor->SetWindowTitle("Havana - " + CurrentScene->Path.LocalPath);
#endif

	if (!CurrentScene->Load(GameWorld))
	{
	}
}
