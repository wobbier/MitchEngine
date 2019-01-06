// 2018 Mitchell Andrews
#pragma once
#include "Engine/Window.h"
#include "Engine/World.h"
#include "Engine/ResourceCache.h"
#include "Utility/Config.h"
#include "Utility/Util.h"
#include "Engine/Clock.h"
#if ME_PLATFORM_UWP
#include "Graphics/Common/DeviceResources.h"
#endif // ME_PLATFORM_UWP

class Game
{
public:
	World * GameWorld;

	long long FrameRate;

	Game();
	virtual ~Game();

#if ME_PLATFORM_UWP
	void Start(const std::shared_ptr<DX::DeviceResources>& deviceResources);
#else
	void Start();
#endif
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

	class AnimationCore* Animator;
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
