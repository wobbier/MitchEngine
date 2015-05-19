// 2015 Mitchell Andrews
#pragma once
#include "Core.h"
#include "Logger.h"
#include "Window.h"
#include "Config.h"
#include <unordered_map>
#include <string>

namespace ma {
	class Engine {
	public:
		Engine() = default;
		~Engine();

		// Initialize our core engine components.
		bool Init();

		// Starts the infinite loop that is the game engine.
		void StartLoop();

		// Update the components.
		void Update(float dt);

		// Add a new core to the engine. (Render, Physics, Input, etc...)
		void Add(std::string name, Core* core);

		// Retrieve the core from then engine for use.
		Core* Get(std::string name);

		Window* GetWindow();

		Logger Log;

		// Remove copy, copy assignment, move, and move assignment constructors.
		Engine(const Engine&) = delete;
		Engine& operator=(const Engine&) = delete;
		Engine(Engine&&) = delete;
		Engine& operator=(Engine&&) = delete;
	protected:
	private:
		Window* GameWindow;
		std::unordered_map<std::string, Core*> Cores;
		Config* EngineConfig;
	};
}