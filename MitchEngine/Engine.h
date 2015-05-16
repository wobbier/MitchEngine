// 2015 Mitchell Andrews
#pragma once
#include "Core.h"
#include <vector>

namespace ma {
	class Engine {
	public:
		Engine() = default;
		~Engine() = default;

		// Initialize our core engine components.
		bool Init();

		// Starts the infinite loop that is the game engine.
		void StartLoop();

		// Update the components.
		void Update(float dt);

		// Add a new core to the engine. (Render, Physics, Input, etc...)
		void Add(Core* core);
	protected:
	private:
		std::vector<Core*> Cores;
	};
}