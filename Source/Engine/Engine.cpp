#include "PCH.h"
#include "Engine.h"
#include "Logger.h"
#include "Config.h"
#include "Window/UWPWindow.h"
#include "Window/Win32Window.h"
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
#include "HavanaEvents.h"
#include "Components/Camera.h"
#include "Components/Cameras/FlyingCamera.h"
#include "Cores/Cameras/FlyingCameraCore.h"
#include "Cores/AudioCore.h"
#include "Cores/UI/UICore.h"

Engine& GetEngine()
{
	return Engine::GetInstance();
}

Engine::Engine()
	: Running(true)
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

	EngineConfig = new Config(Path("Assets\\Config\\Engine.cfg"));

#if ME_PLATFORM_WIN64
	const json& WindowConfig = EngineConfig->GetObject("Window");
	int WindowWidth = WindowConfig["Width"];
	int WindowHeight = WindowConfig["Height"];
	std::function<void(const Vector2&)> Func = [this](const Vector2& NewSize)
	{
		if (m_renderer)
		{
			m_renderer->WindowResized(NewSize);
		}
		if (UI)
		{
			UI->OnResize(MainCamera.OutputSize);
		}
	};
	GameWindow = new Win32Window(EngineConfig->GetValue("Title"), Func, 500, 300, WindowWidth, WindowHeight);
#endif
#if ME_PLATFORM_UWP
	GameWindow = new UWPWindow("MitchEngine", 200, 200);
#endif

	m_renderer = new Moonlight::Renderer();
	m_renderer->WindowResized(GameWindow->GetSize());

	GameWorld = std::make_shared<World>();

	Cameras = new CameraCore();

	SceneNodes = new SceneGraph();

	ModelRenderer = new RenderCore();
	AudioThread = new AudioCore();

	m_renderer->Init();

	UI = new UICore(GameWindow);

	InitGame();

	m_isInitialized = true;
}

void Engine::InitGame()
{
	GameWorld->AddCore<CameraCore>(*Cameras);
	GameWorld->AddCore<SceneGraph>(*SceneNodes);
	GameWorld->AddCore<RenderCore>(*ModelRenderer);
	GameWorld->AddCore<AudioCore>(*AudioThread);
	GameWorld->AddCore<UICore>(*UI);

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
	float lastTime = GameClock.GetTimeInMilliseconds();

	const float FramesPerSec = 144.f;
	const float MaxDeltaTime = (1.f / FramesPerSec);

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

		GameClock.Update();

		AccumulatedTime += GameClock.GetDeltaSeconds();
		if (AccumulatedTime >= MaxDeltaTime)
		{
			float deltaTime = AccumulatedTime;

			GameWorld->Simulate();

			m_input.Update();

			// Update our engine
			GameWorld->UpdateLoadedCores(deltaTime);
			SceneNodes->Update(deltaTime);
			Cameras->Update(0.0f);

			{
				OPTICK_CATEGORY("MainLoop::GameUpdate", Optick::Category::GameLogic);
				m_game->OnUpdate(deltaTime);
			}
			
			AudioThread->Update(deltaTime);
			ModelRenderer->Update(deltaTime);

			if (UI)
			{
				UI->OnResize(MainCamera.OutputSize);
			}
			UI->Update(deltaTime);

#if !ME_EDITOR
			Vector2 MainOutputSize = m_renderer->GetDevice().GetOutputSize();
			MainCamera.Position = Camera::CurrentCamera->Position;
			MainCamera.Front = Camera::CurrentCamera->Front;
			MainCamera.Up = Camera::CurrentCamera->Up;
			MainCamera.OutputSize = MainOutputSize;
			MainCamera.FOV = Camera::CurrentCamera->GetFOV();
			MainCamera.Skybox = Camera::CurrentCamera->Skybox;
			MainCamera.ClearColor = Camera::CurrentCamera->ClearColor;
			MainCamera.ClearType = Camera::CurrentCamera->ClearType;
			MainCamera.Projection = Camera::CurrentCamera->Projection;
			MainCamera.OrthographicSize = Camera::CurrentCamera->OrthographicSize;

			EditorCamera = MainCamera;
#endif

			m_renderer->Render([this]() {
				m_game->PostRender();
			}, [this]() {
				UI->Render();
			}, MainCamera, EditorCamera);

			AccumulatedTime = std::fmod(AccumulatedTime, MaxDeltaTime);
		}

		Sleep(1);
	}
	EngineConfig->Save();
}

bool Engine::OnEvent(const BaseEvent& evt)
{
	if (evt.GetEventId() == LoadSceneEvent::GetEventId())
	{
		const LoadSceneEvent& test = static_cast<const LoadSceneEvent&>(evt);
		//InputEnabled = test.Enabled;
		LoadScene(test.Level);
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

Config& Engine::GetConfig() const
{
	return *EngineConfig;
}

Input& Engine::GetInput()
{
	return m_input;
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

	if (!CurrentScene->Load(GameWorld))
	{
	}

	SceneLoadedEvent evt;
	evt.LoadedScene = CurrentScene;
	evt.Fire();
}
