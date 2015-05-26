// Source3 Engine - 2014 Mitchell Andrews
#pragma once
#include "Window.h"
#include "Renderer.h"
#include "World.h"
#include "Logger.h"
#include "Config.h"

namespace ma {
	class Game {
	public:
		World* GameWorld;
		Logger Log;

		Game();
		virtual ~Game();

		void Start();
		virtual void Initialize() = 0;
		virtual void Update(float DeltaTime) = 0;
		virtual void Render() = 0;
		virtual void End() = 0;

		bool IsRunning() const;
		void Quit();

		Window* GetWindow();

		// Remove copy, copy assignment, move, and move assignment constructors.
		Game(const Game&) = delete;
		Game& operator=(const Game&) = delete;
		Game(Game&&) = delete;
		Game& operator=(Game&&) = delete;
	private:
		bool Running;
		Window* GameWindow;
		Config* EngineConfig;
	};
}