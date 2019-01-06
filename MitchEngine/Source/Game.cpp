#include "PCH.h"
#include "Game.h"
#include "Utility/Logger.h"
#include "Engine/Clock.h"
#include "Cores/Rendering/RenderCore.h"
#include "Cores/Rendering/DifferedLighting.h"
#include "Cores/Camera/CameraCore.h"
#include "Cores/AnimationCore.h"
#include "Cores/PhysicsCore.h"
#include "Cores/SceneGraph.h"
#include "Graphics/Cubemap.h"
#include "Engine/Input.h"

Game::Game()
	: Running(true)
	, GameClock(Clock::GetInstance())
{
}

Game::~Game()
{
}
bool my_tool_active = false;
#if ME_PLATFORM_UWP
void Game::Start(const std::shared_ptr<DX::DeviceResources>& deviceResources)
#else
void Game::Start()
#endif
{
	Logger::GetInstance().SetLogFile("engine.txt");
	Logger::GetInstance().SetLogPriority(Logger::LogType::Info);
	Logger::GetInstance().Log(Logger::LogType::Info, "Starting the MitchEngine.");

	EngineConfig = new Config("Assets\\Config\\Engine.cfg");

	//auto WindowConfig = EngineConfig->Root["window"];
	int WindowWidth = 1280;//WindowConfig["width"].asInt();
	int WindowHeight = 720;//WindowConfig["height"].asInt();

#if ME_PLATFORM_WIN64
	GameWindow = new Window("MitchEngine", WindowWidth, WindowHeight);
#endif

	GameWorld = new World();

#if ME_PLATFORM_WIN64
	LightingRenderer = new DifferedLighting();
	GameWorld->AddCore<DifferedLighting>(*LightingRenderer);
#endif
	Animator = new AnimationCore();
	GameWorld->AddCore<AnimationCore>(*Animator);

	Physics = new PhysicsCore();
	GameWorld->AddCore<PhysicsCore>(*Physics);

	Cameras = new CameraCore();
	GameWorld->AddCore<CameraCore>(*Cameras);

	SceneNodes = new SceneGraph();
	GameWorld->AddCore<SceneGraph>(*SceneNodes);

#if ME_PLATFORM_UWP
	ModelRenderer = new RenderCore(deviceResources);
#endif
#if ME_PLATFORM_WIN64
	ModelRenderer = new RenderCore();
#endif
	GameWorld->AddCore<RenderCore>(*ModelRenderer);

	Initialize();

	GameClock.Reset();
}

void Game::Tick()
{
	// Game loop
#if ME_PLATFORM_WIN64
	while (!GameWindow->ShouldClose())
	{
		BROFILER_FRAME("MainLoop")
			// Check and call events
			GameWindow->PollInput();
#endif

		float time = GameClock.GetTimeInMilliseconds();
		const float deltaTime = GameClock.deltaTime = (time <= 0.0f || time >= 0.3) ? 0.0001f : time;

		// Update our engine
		GameWorld->Simulate();
		Physics->Update(deltaTime);
		Animator->Update(deltaTime);

		Update(deltaTime);

		SceneNodes->Update(deltaTime);

		Cameras->Update(deltaTime);

		ModelRenderer->Update(deltaTime);

#if ME_PLATFORM_WIN64
		LightingRenderer->PreRender();
#endif

		ModelRenderer->Render();

#if ME_PLATFORM_WIN64
		LightingRenderer->PostRender();
		// Swap the buffers
		GameWindow->Swap();
	}
	glfwTerminate();
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
	//ModelRenderer->CreateWindowSizeDependentResources();
}

bool Game::IsRunning() const
{
	return true;
}

//bool Game::IsRunning() const { return Running; }

void Game::Quit() { Running = false; }