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

	ResourceCache& GetResources();

	bool IsRunning() const;
	void Quit();

	Window* GetWindow();

	class PhysicsCore* Physics;
	class CameraCore* Cameras;
	class SceneGraph* SceneNodes;
	class RenderCore* ModelRenderer;
	class DifferedLighting* LightingRenderer;
	Clock& GameClock;
private:
	bool Running;
	Window* GameWindow;
	Config* EngineConfig;
	ResourceCache Resources;
};
