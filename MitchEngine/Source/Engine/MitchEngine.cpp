#include "PCH.h"
#include "MitchEngine.h"
#include "Logger.h"
#include "Config.h"
#include "Window/UWPWindow.h"
#include "Window/D3D12Window.h"
#include "Events/EventManager.h"
#include "Cores/PhysicsCore.h"
#include "Cores/Camera/CameraCore.h"
#include "Cores/SceneGraph.h"
#include "Cores/Rendering/RenderCore.h"
#include "Game.h"
#include "Window/IWindow.h"
#include "Input.h"

MitchEngine::MitchEngine()
	: Running(true)
	, GameClock(Clock::GetInstance())
{
}

MitchEngine::~MitchEngine()
{

}

void MitchEngine::Init(Game* game)
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
	int WindowWidth = 1280;//WindowConfig["width"].asInt();
	int WindowHeight = 720;//WindowConfig["height"].asInt();

#if ME_PLATFORM_WIN64
	GameWindow = new D3D12Window("MitchEngine", WindowWidth, WindowHeight);
#endif
#if ME_PLATFORM_UWP
	GameWindow = new UWPWindow("MitchEngine", WindowWidth, WindowHeight);
#endif

	m_renderer = new Moonlight::Renderer();

	GameWorld = std::make_shared<World>();

	Physics = new PhysicsCore();
	GameWorld->AddCore<PhysicsCore>(*Physics);

	Cameras = new CameraCore();
	GameWorld->AddCore<CameraCore>(*Cameras);

	SceneNodes = new SceneGraph();
	GameWorld->AddCore<SceneGraph>(*SceneNodes);

	ModelRenderer = new RenderCore();
	GameWorld->AddCore<RenderCore>(*ModelRenderer);

	m_game->Initialize();

	m_isInitialized = true;
}

void MitchEngine::Run()
{
	GameClock.Reset();
	// Game loop
	forever
	{
		BROFILER_FRAME("MainLoop")
			// Check and call events
			GameWindow->ParseMessageQueue();

		if (GameWindow->ShouldClose())
		{
			m_game->End();
			break;
		}

		EventManager::GetInstance().FirePendingEvents();

		float time = GameClock.GetTimeInMilliseconds();
		const float deltaTime = GameClock.deltaTime = (time <= 0.0f || time >= 0.3) ? 0.0001f : time;


		// Update our engine
		GameWorld->Simulate();
		Physics->Update(deltaTime);

		m_game->Update(deltaTime);

		SceneNodes->Update(deltaTime);

		Cameras->Update(deltaTime);

		ModelRenderer->Update(deltaTime);

		ModelRenderer->Render();
	}
}

Moonlight::Renderer& MitchEngine::GetRenderer()
{
	return *m_renderer;
}

std::weak_ptr<World> MitchEngine::GetWorld() const
{
	return GameWorld;
}

ResourceCache& MitchEngine::GetResources()
{
	return Resources;
}

const bool MitchEngine::IsInitialized() const
{
	return m_isInitialized;
}

bool MitchEngine::IsRunning() const
{
	return true;
}

void MitchEngine::Quit() { Running = false; }

IWindow* MitchEngine::GetWindow()
{
	return GameWindow;
}
