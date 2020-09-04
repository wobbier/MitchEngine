#include "PCH.h"
#include "Engine.h"
#include "CLog.h"
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
#include "Events/SceneEvents.h"
#include "Components/Camera.h"
#include "Components/Cameras/FlyingCamera.h"
#include "Cores/Cameras/FlyingCameraCore.h"
#include "Cores/AudioCore.h"
#include "Cores/UI/UICore.h"

#if ME_EDITOR
#include "Utils/StringUtils.h"
#include <fileapi.h>
#endif
#include "Resource/ResourceCache.h"
#include "optick.h"
#include "Core/JobSystem.h"
#include "Work/Burst.h"

Engine& GetEngine()
{
	return Engine::GetInstance();
}

Engine::Engine()
	: Running(true)
	, m_jobSystem(8)
{
	std::vector<TypeId> events;
	events.push_back(LoadSceneEvent::GetEventId());
	EventManager::GetInstance().RegisterReceiver(this, events);
}

Engine::~Engine()
{
	delete EngineConfig;
}

void Engine::Init(Game* game)
{
	if (m_isInitialized || !game)
	{
		return;
	}

	m_game = game;

	CLog::GetInstance().SetLogFile("Engine.txt");
	CLog::GetInstance().SetLogPriority(CLog::LogType::Info);
	CLog::GetInstance().Log(CLog::LogType::Info, "Starting the MitchEngine.");
	Path engineCfg("Assets\\Config\\Engine.cfg");

#if ME_EDITOR
	if (engineCfg.FullPath.rfind("Engine") != -1)
	{
		Path gameEngineCfgPath("Assets\\Config\\Engine.cfg", true);
		if (!gameEngineCfgPath.Exists)
		{
			CreateDirectory(StringUtils::ToWString(gameEngineCfgPath.Directory).c_str(), NULL);
			File gameEngineCfg = File(engineCfg);
			File newGameConfig(gameEngineCfgPath);
			newGameConfig.Write(gameEngineCfg.Read());
		}
	}
#endif

	EngineConfig = new Config(engineCfg);

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
			if (Camera::CurrentCamera)
			{
				UI->OnResize(Camera::CurrentCamera->OutputSize);
			}
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

	const float FramesPerSec = 240.f;
	const float MaxDeltaTime = (1.f / FramesPerSec);

	burst.InitializeWorkerThreads();
	// Game loop
	forever
	{
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
			OPTICK_FRAME("MainLoop");
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

			// editor only?
			if (UI)
			{
				if (Camera::CurrentCamera)
				{
					UI->OnResize(Camera::CurrentCamera->OutputSize);
				}
			}
			UI->Update(deltaTime);
//
//#if !ME_EDITOR
//			Vector2 MainOutputSize = m_renderer->GetDevice().GetOutputSize();
//			MainCamera.Position = Camera::CurrentCamera->Position;
//			MainCamera.Front = Camera::CurrentCamera->Front;
//			MainCamera.OutputSize = MainOutputSize;
//			MainCamera.FOV = Camera::CurrentCamera->GetFOV();
//			MainCamera.Skybox = Camera::CurrentCamera->Skybox;
//			MainCamera.ClearColor = Camera::CurrentCamera->ClearColor;
//			MainCamera.ClearType = Camera::CurrentCamera->ClearType;
//			MainCamera.Projection = Camera::CurrentCamera->Projection;
//			MainCamera.OrthographicSize = Camera::CurrentCamera->OrthographicSize;
//
//			EditorCamera = MainCamera;
//#endif

			m_renderer->ThreadedRender([this]() {
				m_game->PostRender();
			}, [this]() {
				UI->Render();
			}, EditorCamera);

			AccumulatedTime = std::fmod(AccumulatedTime, MaxDeltaTime);
		}

		ResourceCache::GetInstance().Dump();

		//Sleep(1);
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

JobQueue& Engine::GetJobQueue()
{
	return m_jobSystem.GetJobQueue();
}

JobSystem& Engine::GetJobSystem()
{
	return m_jobSystem;
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

Burst& Engine::GetBurstWorker()
{
	return burst;
}

void Engine::LoadScene(const std::string& SceneFile)
{
	Cameras->Init();
	if (CurrentScene)
	{
		CurrentScene->UnLoad();
		delete CurrentScene;
		CurrentScene = nullptr;
	}

	GameWorld->Unload();
	SceneNodes->Init();
	CurrentScene = new Scene(SceneFile);

	if (!CurrentScene->Load(GameWorld))
	{
	}

	SceneLoadedEvent evt;
	evt.LoadedScene = CurrentScene;
	evt.Fire();
#if !ME_EDITOR
	//GameWorld->Start();
#endif
}
