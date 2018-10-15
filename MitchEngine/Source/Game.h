// 2018 Mitchell Andrews
#pragma once
#include "Engine/Window.h"
#include "Engine/World.h"
#include "Engine/ResourceCache.h"
#include "Utility/Config.h"
#include "Utility/Util.h"
#include "Engine/Clock.h"

class Game
{
public:
	World * GameWorld;

	long long FrameRate;

	Game();
	virtual ~Game();

	void Start();
	void Tick();
	virtual void Initialize();
	virtual void Update(float DeltaTime);
	virtual void End();

	bool IsRunning() const;
	void Quit();

	//Window* GetWindow();

	// Remove copy, copy assignment, move, and move assignment constructors.
	class AnimationCore* Animator;
	class PhysicsCore* Physics;
	class CameraCore* Cameras;
	class SceneGraph* SceneNodes;
	Clock& GameClock;
private:
	bool Running;
	//Window* GameWindow;
	Config* EngineConfig;
};
