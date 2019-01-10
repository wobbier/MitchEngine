// 2018 Mitchell Andrews
#pragma once
#include "Engine/Window.h"
#include "Engine/World.h"
#include "Resource/ResourceCache.h"
#include "Config.h"
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
	bool Render();
	void WindowResized();

	bool IsRunning() const;
	void Quit();

#if ME_PLATFORM_WIN64
	Window* GetWindow();
#endif

	class PhysicsCore* Physics;
	class CameraCore* Cameras;
	class SceneGraph* SceneNodes;
	class RenderCore* ModelRenderer;
	class DifferedLighting* LightingRenderer;
	Clock& GameClock;
private:
	bool Running;
#if ME_PLATFORM_WIN64
	Window* GameWindow;
#endif
	Config* EngineConfig;
};
