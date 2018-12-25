// 2018 Mitchell Andrews
#pragma once
#include "Engine/Window.h"
#include "Engine/World.h"
#include "Engine/ResourceCache.h"
#include "Utility/Config.h"
#include "Utility/Util.h"
#include "Engine/Clock.h"
#include "Graphics/Common/DeviceResources.h"

class Game
{
public:
	World * GameWorld;

	long long FrameRate;

	Game();
	virtual ~Game();

	void Start(const std::shared_ptr<DX::DeviceResources>& deviceResources);
	void Tick();
	virtual void Initialize();
	virtual void Update(float DeltaTime);
	virtual void End();
	bool Render();
	void WindowResized();

	bool IsRunning() const;
	void Quit();

#ifdef ME_PLATFORM_WIN64
	Window* GetWindow();
#endif

	class AnimationCore* Animator;
	class PhysicsCore* Physics;
	class CameraCore* Cameras;
	class SceneGraph* SceneNodes;
	class Renderer* ModelRenderer;
	Clock& GameClock;
private:
	bool Running;
#ifdef ME_PLATFORM_WIN64
	Window* GameWindow;
#endif
	Config* EngineConfig;
};
