#pragma once
#include "Renderer.h"
#include "World.h"
#include "Dementia.h"
#include "Clock.h"

class Game;
class IWindow;

class Engine
{
public:
	const float FPS = 144.0f;
	long long FrameRate;

	Engine();
	~Engine();

	void Init(Game* game);

	void InitGame();
	void StartGame();
	void StopGame();
	
	void Run();

	Moonlight::Renderer& GetRenderer() const;

	std::weak_ptr<World> GetWorld() const;
	bool IsRunning() const;
	void Quit();
	const bool IsInitialized() const;

	IWindow* GetWindow();

	const bool IsGameRunning() const;

	const bool IsGamePaused() const;

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
	class Config* EngineConfig;

	Game* m_game;
	float AccumulatedTime = 0.0f;
	bool m_isInitialized = false;
	bool m_isGameRunning = false;
	bool m_isGamePaused = false;

#if ME_EDITOR
	std::unique_ptr<class Havana> Editor;
	class EditorCore* EditorSceneManager = nullptr;
#endif
};