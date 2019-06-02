#pragma once
#include "Renderer.h"
#include "World.h"
#include "Dementia.h"
#include "Clock.h"
#include "File.h"
#include "Events/EventReceiver.h"
#include "World/Scene.h"
#include "RenderCommands.h"

class Game;
class IWindow;

class Engine
	: public EventReceiver
{
public:
	const float FPS = 144.0f;
	long long FrameRate;

	Engine();
	~Engine();

	void Init(Game* game);

	void InitGame();

	void StopGame();

	void LoadScene(const std::string& Level);
	
	void Run();
	virtual bool OnEvent(const BaseEvent& evt);

	Moonlight::Renderer& GetRenderer() const;

	std::weak_ptr<World> GetWorld() const;
	bool IsRunning() const;
	void Quit();
	const bool IsInitialized() const;

	IWindow* GetWindow();

	class CameraCore* Cameras = nullptr;
	class SceneGraph* SceneNodes = nullptr;
	class RenderCore* ModelRenderer = nullptr;
	Clock& GameClock;
	Moonlight::CameraData MainCamera;
	Moonlight::CameraData EditorCamera;
	Scene* CurrentScene = nullptr;
private:
	Moonlight::Renderer* m_renderer = nullptr;
	std::shared_ptr<World> GameWorld;
	bool Running = false;
	IWindow* GameWindow = nullptr;
	class Config* EngineConfig = nullptr;
	Game* m_game = nullptr;
	float AccumulatedTime = 0.0f;
	float FrameTime = 0.0f;
	bool m_isInitialized = false;

	ME_SINGLETON_DEFINITION(Engine)
};

Engine& GetEngine();