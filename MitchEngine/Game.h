// 2015 Mitchell Andrews
#pragma once
#include "Window.h"
#include "Renderer.h"
#include "World.h"
#include "Config.h"
#include "Util.h"

namespace ma {
	class Game {
	public:
		World* GameWorld;

		long long FrameRate;

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
		MA_NONCOPYABLE(Game);
		MA_NONMOVABLE(Game);
	private:
		bool Running;
		Window* GameWindow;
		Config* EngineConfig;
	};
}