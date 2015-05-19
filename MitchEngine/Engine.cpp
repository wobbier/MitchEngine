#include "Engine.h"
#include "Renderer.h"
#include "Window.h"

using namespace ma;

Engine::~Engine() {

}

bool Engine::Init() {
	Log.SetLogFile("engine.txt");
	Log.SetLogPriority(Logger::INFO);

	Add("Renderer", new Renderer());

	Window* window = new Window("YO", 800, 600);
	return true;
}

void Engine::StartLoop() {
	while (true) {
		Update(60.0f / 100.0f);
	}
}

void Engine::Update(float dt) {
	for (auto& i : Cores) {
		i.second->Update(dt);
	}
}

void Engine::Add(std::string name, Core* core) {
	core->Init(this);
	std::pair<std::string, Core*> NewCore(name, core);
	Cores.insert(NewCore);
}