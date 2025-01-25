#pragma once
#include "Pointers.h"
#include "Events/EventManager.h"
#include "Events/Event.h"
#include "Components/Transform.h"
#include "ECS/Core.h"
#include "ECS/EntityHandle.h"
#include <Resource/MetaFile.h>
#include <Path.h>
#include <Types/AssetType.h>

namespace Moonlight {
    class Texture;
}

class TestEditorEvent
    : public Event<TestEditorEvent>
{
public:
    TestEditorEvent()
        : Event()
    {
    }
    std::string Path;
};

class InspectEvent
    : public Event<InspectEvent>
{
public:
    InspectEvent()
        : Event()
    {
    }
    EntityHandle SelectedEntity;
    WeakPtr<Transform> SelectedTransform;
    BaseCore* SelectedCore = nullptr;
};

class ClearInspectEvent
    : public Event<ClearInspectEvent>
{
public:
    ClearInspectEvent()
        : Event()
    {
    }
};