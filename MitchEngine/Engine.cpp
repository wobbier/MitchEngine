#include "Engine.h"
#include "Renderer.h"
#include "Window.h"

using namespace ma;

Engine::~Engine() {
	Log.Log(Logger::INFO, "Mitch Engine shutting down.");
}

bool Engine::Init() {
	Log.SetLogFile("engine.txt");
	Log.SetLogPriority(Logger::INFO);
	Log.Log(Logger::INFO, "Starting the MitchEngine.");

	EngineConfig = new Config("Config\\Engine.cfg");

	auto WindowConfig = EngineConfig->Root["window"];
	int WindowWidth = WindowConfig["width"].asInt();
	int WindowHeight = WindowConfig["height"].asInt();

	GameWindow = new Window(EngineConfig->GetValue("title"), WindowWidth, WindowHeight);

	Add("Renderer", new Renderer());

	// Initialize all the cores.
	for (auto& c : Cores) {
		c.second->Init(this);
	}
	return true;
}

void Engine::StartLoop() {
	while (!GameWindow->ShouldClose()) {
		Update(60.0f / 100.0f);
	}
}

void Engine::Update(float dt) {
	for (auto& c : Cores) {
		c.second->Update(dt);
	}
}

void Engine::Add(std::string name, Core* core) {
	std::pair<std::string, Core*> NewCore(name, core);
	Cores.insert(NewCore);
}

Core* Engine::Get(std::string name) {
	return Cores[name];
}

Window* Engine::GetWindow() {
	return GameWindow;
}

