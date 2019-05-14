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

Engine::Engine()
	: Running(true)
	, GameClock(Clock::GetInstance())
	, CurrentLevel(FilePath("Assets/Alley.lvl"))
{
	std::vector<TypeId> events;
	events.push_back(NewSceneEvent::GetEventId());
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

	Logger::GetInstance().SetLogFile("engine.txt");
	Logger::GetInstance().SetLogPriority(Logger::LogType::Info);
	Logger::GetInstance().Log(Logger::LogType::Info, "Starting the MitchEngine.");

	EngineConfig = new Config("Assets\\Config\\Engine.cfg");

	//auto WindowConfig = EngineConfig->Root["window"];
	int WindowWidth = 1920;//WindowConfig["width"].asInt();
	int WindowHeight = 1080;//WindowConfig["height"].asInt();

#if ME_PLATFORM_WIN64
	GameWindow = new D3D12Window("MitchEngine", WindowWidth, WindowHeight);
#endif
#if ME_PLATFORM_UWP
	GameWindow = new UWPWindow("MitchEngine", WindowWidth, WindowHeight);
#endif

	m_renderer = new Moonlight::Renderer();
	m_renderer->WindowResized(GameWindow->GetSize());

	GameWorld = std::make_shared<World>();

	Physics = new PhysicsCore();

	Cameras = new CameraCore();

	SceneNodes = new SceneGraph();

	ModelRenderer = new RenderCore();

	FlyingCameraController = new FlyingCameraCore();

#if ME_EDITOR
	Editor = std::make_unique<Havana>(this, m_renderer);
	EditorSceneManager = new EditorCore(Editor.get());
	InitGame();
	LoadLevel();
#else
	InitGame();
	LoadLevel();
	StartGame();
#endif


	m_isInitialized = true;
}

void Engine::InitGame()
{
	GameWorld->AddCore<PhysicsCore>(*Physics);
	GameWorld->AddCore<CameraCore>(*Cameras);
	GameWorld->AddCore<SceneGraph>(*SceneNodes);
	GameWorld->AddCore<RenderCore>(*ModelRenderer);

	GameWorld->AddCore<FlyingCameraCore>(*FlyingCameraController);
#if ME_EDITOR
	GameWorld->AddCore<EditorCore>(*EditorSceneManager);
#endif
	m_game->OnInitialize();
}

void Engine::StartGame()
{
	if (!m_isGameRunning)
	{
		m_isGameRunning = true;
		m_game->OnStart();
	}
}

void Engine::StopGame()
{
	m_game->OnEnd();
}

void Engine::Run()
{
	m_renderer->Init();
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

#if ME_EDITOR
		Editor->NewFrame([this]() {
			if (!m_isGameRunning)
			{
				StartGame();
			}
			m_isGamePaused = false;
		}
		, [this]() {
			m_isGamePaused = true;
		}
		, [this]() {
			m_isGameRunning = false;
			m_isGamePaused = false;

			LoadLevel();
		});

		EditorSceneManager->Update(deltaTime, SceneNodes->RootTransform);

		Editor->UpdateWorld(GameWorld.get(), SceneNodes->RootTransform, EditorSceneManager->GetEntities());

#endif
		GameWorld->Simulate();

		AccumulatedTime += deltaTime;

		if (IsGameRunning() && !IsGamePaused())
		{

			// Update our engine

			{
				OPTICK_CATEGORY("MainLoop::GameUpdate", Optick::Category::GameLogic);
				m_game->OnUpdate(deltaTime);
			}

			Physics->Update(deltaTime);
		}
#if ME_EDITOR
		if(Editor->IsGameFocused())
		{
			//FlyingCameraController->SetCamera(Camera::CurrentCamera);
		}
		else
#endif
		{
			//FlyingCameraController->SetCamera(Camera::EditorCamera);
		}

		FlyingCameraController->Update(deltaTime);

			SceneNodes->Update(deltaTime);

			Cameras->Update(deltaTime);

			ModelRenderer->Update(AccumulatedTime);

			AccumulatedTime -= 1.0f / FPS;

#if ME_EDITOR
			Vector2 MainOutputSize = Editor->GetGameOutputSize();
			Moonlight::CameraData MainCamera = { Camera::CurrentCamera->Position, Camera::CurrentCamera->Front, Camera::CurrentCamera->Up, MainOutputSize, Camera::CurrentCamera->GetFOV() };
			Moonlight::CameraData EditorCamera = { Camera::EditorCamera->Position, Camera::EditorCamera->Front, Camera::EditorCamera->Up, Editor->WorldViewRenderSize, Camera::EditorCamera->GetFOV() };
#else
			Vector2 MainOutputSize = m_renderer->GetDevice().GetOutputSize();
			Moonlight::CameraData MainCamera = { Camera::CurrentCamera->Position, Camera::CurrentCamera->Front, Camera::CurrentCamera->Up, MainOutputSize, Camera::CurrentCamera->GetFOV() };
			Moonlight::CameraData& EditorCamera = MainCamera;
#endif

			m_renderer->Render([this]() {
#if ME_EDITOR
				Editor->Render();
#endif
			}, MainCamera, EditorCamera);

			Sleep(6);
	}
}

bool Engine::OnEvent(const BaseEvent& evt)
{
	if (evt.GetEventId() == NewSceneEvent::GetEventId())
	{
		const NewSceneEvent& test = static_cast<const NewSceneEvent&>(evt);
		//InputEnabled = test.Enabled;
		GameWorld->Cleanup();
		InitGame();
		GameWorld->Simulate();

		return true;
	}

	if (evt.GetEventId() == LoadSceneEvent::GetEventId())
	{
		const LoadSceneEvent& test = static_cast<const LoadSceneEvent&>(evt);
		//InputEnabled = test.Enabled;
		LoadLevel(test.Level);

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

const bool Engine::IsGameRunning() const
{
	return m_isGameRunning;
}

const bool Engine::IsGamePaused() const
{
	return m_isGamePaused;
}

void Engine::LoadLevel()
{
#if ME_EDITOR
	Editor->SetWindowTitle("Havana - " + CurrentLevel.Path.LocalPath);
#endif
	GameWorld->Cleanup();
	InitGame();
	//InitGame();
	CurrentLevel.Read();

	if (CurrentLevel.Data.length() > 0)
	{
		json level;

		level = json::parse(CurrentLevel.Data);
		json& scene = level["Scene"];
		for (json& ent : scene)
		{
			LoadSceneObject(ent, nullptr);
		}

	}
	//ComponentFactory::GetFactory(std::string("Transform"))->Create(ent, "Transform");


	GameWorld->Simulate();
}

void Engine::LoadLevel(std::string Level)
{
	CurrentLevel = File(FilePath(Level));
	LoadLevel();
}

void Engine::LoadSceneObject(const json& obj, class Transform* parent)
{
	auto newEnt = GameWorld->CreateEntity();

	Transform* transComp = nullptr;
	for (const json& comp : obj["Components"])
	{
		BaseComponent* addedComp = newEnt.lock()->AddComponentByName(comp["Type"]);
		if (comp["Type"] == "Transform")
		{
			transComp = static_cast<Transform*>(addedComp);
			if (parent)
			{
				transComp->SetParent(*parent);
			}
			transComp->SetName(obj["Name"]);
		}
		if (addedComp)
		{
			addedComp->Deserialize(comp);
		}
	}

	if (obj.contains("Children"))
	{
		for (const json& child : obj["Children"])
		{
			LoadSceneObject(child, transComp);
		}
	}
}
