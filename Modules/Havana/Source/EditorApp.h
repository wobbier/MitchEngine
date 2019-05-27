#pragma once
#include <DirectXMath.h>
#include "Game.h"
#include <glm.hpp>
#include "Havana.h"
#include "Events/EventReceiver.h"

class EditorApp
	: public Game
	, public EventReceiver
{
public:
	EditorApp();
	virtual ~EditorApp() override;

	virtual void OnInitialize() override;

	virtual void OnStart() override;
	virtual void OnUpdate(float DeltaTime) override;
	virtual void OnEnd() override;

	virtual void PostRender() override;

	WeakPtr<Entity> MainCamera;
	std::unique_ptr<Havana> Editor;
	class EditorCore* EditorSceneManager = nullptr;

	virtual bool OnEvent(const BaseEvent& evt) override;
};
