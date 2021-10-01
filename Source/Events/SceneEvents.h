#pragma once
#include "Events/EventManager.h"
#include "Events/Event.h"

class SaveSceneEvent
	: public Event<SaveSceneEvent>
{
public:
	bool SaveAs = false;
};

class NewSceneEvent
	: public Event<NewSceneEvent>
{
public:
	bool thing = false;
};

class LoadSceneEvent
	: public Event<LoadSceneEvent>
{
public:
	LoadSceneEvent()
		: Event()
	{
	}

	std::string Level;
};

class SceneLoadedEvent
	: public Event<SceneLoadedEvent>
{
public:
	SceneLoadedEvent()
		: Event()
	{
	}

	class Scene* LoadedScene = nullptr;
};