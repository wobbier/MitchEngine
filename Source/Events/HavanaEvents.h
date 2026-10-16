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

class InspectEvent
    : public Event<InspectEvent>
{
public:
    InspectEvent()
        : Event()
    {
    }
    EntityHandle SelectedEntity;
    TransformHandle SelectedTransform;
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

// Queued from component inspectors so structural changes happen after drawing the inspector.
class ModelHierarchyEvent : public Event<ModelHierarchyEvent>
{
public:
    EntityHandle Target;
    std::string NodePath; // empty: compact the hierarchy; otherwise expose this source node
};
