// 2018 Mitchell Andrews
#pragma once
#include "Engine/Window.h"
#include "Engine/World.h"
#include "Engine/ResourceCache.h"
#include "Utility/Config.h"
#include "Utility/Util.h"

class Game
{
public:
	World * GameWorld;

	long long FrameRate;

	Game();
	virtual ~Game();

	void Start();
	virtual void Initialize() = 0;
	virtual void Update(float DeltaTime) = 0;
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
