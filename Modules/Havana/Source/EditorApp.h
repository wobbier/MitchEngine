#pragma once
#include "Game.h"
#include "Havana.h"
#include "Events/EventReceiver.h"

// I don't like this
#include "../../Game/Source/ComponentRegistry.h"
class Engine;

class EditorApp
	: public Game
	, public EventReceiver
{
public:
	EditorApp();
	~EditorApp();

	virtual void OnInitialize() override;

	virtual void OnStart() override;
	virtual void OnUpdate(float DeltaTime) override;

	void UpdateCameras();

	virtual void OnEnd() override;

	virtual void PostRender() override;

	void StartGame();
	void StopGame();

	const bool IsGameRunning() const;

	const bool IsGamePaused() const;

	WeakPtr<Entity> MainCamera;
	std::unique_ptr<Havana> Editor;
	class EditorCore* EditorSceneManager = nullptr;

	virtual bool OnEvent(const BaseEvent& evt) override;
	bool m_isGameRunning = false;
	bool m_isGamePaused = false;
	std::string InitialLevel;
};
