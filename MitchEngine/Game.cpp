#include "Game.h"

using namespace ma;

Game::Game() : Running(true) {
}

Game::~Game() {

}

void Game::Start() {
	Log.SetLogFile("engine.txt");
	Log.SetLogPriority(Logger::INFO);
	Log.Log(Logger::INFO, "Starting the MitchEngine.");

	EngineConfig = new Config("Config\\Engine.cfg");

	auto WindowConfig = EngineConfig->Root["window"];
	int WindowWidth = WindowConfig["width"].asInt();
	int WindowHeight = WindowConfig["height"].asInt();

	GameWindow = new Window(EngineConfig->GetValue("title"), WindowWidth, WindowHeight);

	GameWorld = new World();

	Initialize();
	// Game loop
	while (!GameWindow->ShouldClose()) {
		// Check and call events
		glfwPollEvents();
		//float time = Time::Get()->GetTimeInMilliseconds();
		//Time::Get()->deltaTime = (time <= 0.0f || time >= 0.3) ? 0.0001f : time;
		// Update our engine
		Update(100.0f/30.0f);
		Render();
		// Swap the buffers
		glfwSwapBuffers(GameWindow->window);
	}
	glfwTerminate();
}

bool Game::IsRunning() const { return Running; }

void Game::Quit() { Running = false; }