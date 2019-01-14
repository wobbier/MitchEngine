#include "PCH.h"
#include "Game.h"
#include "Logger.h"
#include "Engine/Clock.h"
#include "Cores/Rendering/RenderCore.h"
#include "Cores/Rendering/DifferedLighting.h"
#include "Cores/Camera/CameraCore.h"
#include "Cores/PhysicsCore.h"
#include "Cores/SceneGraph.h"
#include "Graphics/Cubemap.h"
#include "Engine/Input.h"
#include "Events/EventManager.h"

#if ME_DIRECTX
#include "Window/D3D12Window.h"
#elif ME_OPENGL
#include "Window/GLWindow.h"
#endif

Game::Game()
	: Running(true)
	, GameClock(Clock::GetInstance())
{
}

Game::~Game()
{
}
bool my_tool_active = false;
void Game::Start()
{
	Logger::GetInstance().SetLogFile("engine.txt");
	Logger::GetInstance().SetLogPriority(Logger::LogType::Info);
	Logger::GetInstance().Log(Logger::LogType::Info, "Starting the MitchEngine.");

	EngineConfig = new Config("Assets\\Config\\Engine.cfg");

	//auto WindowConfig = EngineConfig->Root["window"];
	int WindowWidth = 1280;//WindowConfig["width"].asInt();
	int WindowHeight = 720;//WindowConfig["height"].asInt();

#if ME_DIRECTX && !ME_PLATFORM_UWP
	GameWindow = new D3D12Window("MitchEngine", WindowWidth, WindowHeight);
#elif ME_OPENGL
	GameWindow = new GLWindow();
#endif

	GameWorld = new World();

#if ME_OPENGL
	LightingRenderer = new DifferedLighting();
	GameWorld->AddCore<DifferedLighting>(*LightingRenderer);
#endif

	Physics = new PhysicsCore();
	GameWorld->AddCore<PhysicsCore>(*Physics);

	Cameras = new CameraCore();
	GameWorld->AddCore<CameraCore>(*Cameras);

	SceneNodes = new SceneGraph();
	GameWorld->AddCore<SceneGraph>(*SceneNodes);

	ModelRenderer = new RenderCore();
	GameWorld->AddCore<RenderCore>(*ModelRenderer);

	Initialize();

	GameClock.Reset();
	// Game loop
#if ME_PLATFORM_WIN64
	while (true)
	{
		BROFILER_FRAME("MainLoop")
			// Check and call events
			GameWindow->ParseMessageQueue();

		if (GameWindow->ShouldClose())
		{
			End();
			break;
		}
		Tick();
	}
#endif

#if ME_OPENGL
	glfwTerminate();
#endif
}

void Game::Tick()
{
	EventManager::GetInstance().FirePendingEvents();

	float time = GameClock.GetTimeInMilliseconds();
	const float deltaTime = GameClock.deltaTime = (time <= 0.0f || time >= 0.3) ? 0.0001f : time;

	// Update our engine
	GameWorld->Simulate();
	Physics->Update(deltaTime);

	Update(deltaTime);

	SceneNodes->Update(deltaTime);

	Cameras->Update(deltaTime);

	ModelRenderer->Update(deltaTime);

#if ME_OPENGL
	LightingRenderer->PreRender();
#endif

	ModelRenderer->Render();

#if ME_OPENGL
	LightingRenderer->PostRender();
	// Swap the buffers
	GameWindow->Swap();
#endif
}

void Game::Initialize()
{

}

void Game::Update(float DeltaTime)
{

}

void Game::End()
{

}

bool Game::Render()
{
	return ModelRenderer->Render();
}

void Game::WindowResized()
{
}

ResourceCache& Game::GetResources()
{
	return Resources;
}

bool Game::IsRunning() const
{
	return true;
}

//bool Game::IsRunning() const { return Running; }

void Game::Quit() { Running = false; }

#if ME_PLATFORM_WIN64
IWindow* Game::GetWindow()
{
	return GameWindow;
}
#endif