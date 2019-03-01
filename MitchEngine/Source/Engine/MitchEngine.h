#pragma once
#include "Renderer.h"
#include "World.h"

class Game;
class IWindow;

class MitchEngine
{
public:
	const float FPS = 144.0f;
	const float EngineFrequency = 10.0f;

	MitchEngine();
	~MitchEngine();

	void Init(Game* game);
	void Run(class JobScheduler* scheduelr);

	Moonlight::Renderer& GetRenderer() const;

	std::weak_ptr<World> GetWorld() const;
	bool IsRunning() const;
	void Quit();
	const bool IsInitialized() const;

	IWindow* GetWindow();

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

	Game* m_game;
	float AccumulatedTime = 0.0f;
	float AccumulatedFrameTime = 0.0f;
	bool m_isInitialized = false;
};