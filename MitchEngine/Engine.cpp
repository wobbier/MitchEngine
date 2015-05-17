#include "Engine.h"

namespace ma {
	bool Engine::Init() {
		return true;
	}

	void Engine::StartLoop() {
		while (true)
		{
			Update(60.0f / 100.0f);
		}
	}

	void Engine::Update(float dt) {
		for (std::vector<Core*>::iterator i = Cores.begin(); i != Cores.end(); i++)
		{
			Core* c = *i;
			c->Update(dt);
		}
	}

	void Engine::Add(Core* core) {
		Cores.push_back(core);
	}

}