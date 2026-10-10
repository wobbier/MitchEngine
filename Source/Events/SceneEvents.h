#pragma once
#include "Events/EventManager.h"
#include "Events/Event.h"
#include <cstdint>
#include <string>

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
    std::function<void()> Callback;
};

// An additive scene (Engine::LoadSceneAdditive) finished loading, or was unloaded.
class AdditiveSceneEvent
    : public Event<AdditiveSceneEvent>
{
public:
    uint16_t SceneId = 0;
    std::string Path;
    bool Loaded = true;     // false: unloaded
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