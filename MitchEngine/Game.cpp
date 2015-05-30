#include "Game.h"
#include "Logger.h"

using namespace ma;

Game::Game() : Running(true) {
}

Game::~Game() {

}

void Game::Start() {
	Logger::Get().SetLogFile("engine.txt");
	Logger::Get().SetLogPriority(Logger::INFO);
	Logger::Get().Log(Logger::INFO, "Starting the MitchEngine.");

	EngineConfig = new Config("Config\\Engine.cfg");

	auto WindowConfig = EngineConfig->Root["window"];
	int WindowWidth = WindowConfig["width"].asInt();
	int WindowHeight = WindowConfig["height"].asInt();

	GameWindow = new Window(EngineConfig->GetValue("title"), WindowWidth, WindowHeight);

	GameWorld = new World();

	auto SpriteRenderer = new Renderer();
	GameWorld->AddCore<Renderer>(*SpriteRenderer);

	Initialize();
	// Game loop
	while (!GameWindow->ShouldClose()) {
		// Check and call events
		glfwPollEvents();
		//float time = Time::Get()->GetTimeInMilliseconds();
		//Time::Get()->deltaTime = (time <= 0.0f || time >= 0.3) ? 0.0001f : time;
		// Update our engine
		GameWorld->Simulate();
		Update(100.0f / 30.0f);
		SpriteRenderer->Render();
		Render();
		// Swap the buffers
		glfwSwapBuffers(GameWindow->window);
	}
	glfwTerminate();
}

bool Game::IsRunning() const { return Running; }

void Game::Quit() { Running = false; }