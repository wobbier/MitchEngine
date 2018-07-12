#include "Game.h"
#include "Utility/Logger.h"
#include "Engine/Clock.h"
#include "Cores/Rendering/Renderer.h"
#include "Cores/Rendering/DifferedLighting.h"
#include "Cores/Camera/CameraCore.h"
#include "Cores/AnimationCore.h"
#include "Cores/PhysicsCore.h"
#include "Cores/SceneGraph.h"
#include "Graphics/Cubemap.h"

Game::Game() : Running(true)
{
}

Game::~Game()
{
}

void Game::Start()
{
	Logger::GetInstance().SetLogFile("engine.txt");
	Logger::GetInstance().SetLogPriority(Logger::LogType::Info);
	Logger::GetInstance().Log(Logger::LogType::Info, "Starting the MitchEngine.");

	EngineConfig = new Config("Assets\\Config\\Engine.cfg");

	//auto WindowConfig = EngineConfig->Root["window"];
	int WindowWidth = 1280;//WindowConfig["width"].asInt();
	int WindowHeight = 720;//WindowConfig["height"].asInt();

	GameWindow = new Window("MitchEngine", WindowWidth, WindowHeight);

	GameWorld = new World();

	auto ModelRenderer = Renderer();
	GameWorld->AddCore<Renderer>(ModelRenderer);

	auto LightingRenderer = DifferedLighting();
	GameWorld->AddCore<DifferedLighting>(LightingRenderer);

	auto Animator = AnimationCore();
	GameWorld->AddCore<AnimationCore>(Animator);

	auto Physics = PhysicsCore();
	GameWorld->AddCore<PhysicsCore>(Physics);

	auto Cameras = CameraCore();
	GameWorld->AddCore<CameraCore>(Cameras);

	auto SceneNodes = SceneGraph();
	GameWorld->AddCore<SceneGraph>(SceneNodes);

	Initialize();

	Clock& GameClock = Clock::GetInstance();
	GameClock.Reset();

	// Game loop
	while (!GameWindow->ShouldClose())
	{
		// Check and call events
		GameWindow->PollInput();

		float time = GameClock.GetTimeInMilliseconds();
		const float deltaTime = GameClock.deltaTime = (time <= 0.0f || time >= 0.3) ? 0.0001f : time;

		// Update our engine
		GameWorld->Simulate();
		Physics.Update(deltaTime);
		Animator.Update(deltaTime);

		Update(deltaTime);

		SceneNodes.Update(deltaTime);

		Cameras.Update(deltaTime);

		LightingRenderer.PreRender();
		ModelRenderer.Render();
		LightingRenderer.PostRender();

		// Swap the buffers
		GameWindow->Swap();
	}
	glfwTerminate();
}

bool Game::IsRunning() const { return Running; }

void Game::Quit() { Running = false; }