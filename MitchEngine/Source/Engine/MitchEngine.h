#pragma once
#include "Renderer.h"
#include "World.h"

class Game;
class IWindow;

class MitchEngine
{
public:
	long long FrameRate;

	MitchEngine();
	~MitchEngine();

	void Init(Game* game);
	void Run();

	Moonlight::Renderer& GetRenderer();

	std::weak_ptr<World> GetWorld() const;
	bool IsRunning() const;
	void Quit();

	IWindow* GetWindow();

	ResourceCache& GetResources();

	class PhysicsCore* Physics;
	class CameraCore* Cameras;
	class SceneGraph* SceneNodes;
	class RenderCore* ModelRenderer;
	Clock& GameClock;

private:
	Moonlight::Renderer* m_renderer;
	std::shared_ptr<World> GameWorld;
	bool Running;
	IWindow* GameWindow;
	Config* EngineConfig;
	ResourceCache Resources;

	Game* m_game;

	bool m_isInitialized = false;
};