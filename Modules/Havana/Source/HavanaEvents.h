#pragma once
#include "Pointers.h"
#include "Events/EventManager.h"
#include "Events/Event.h"
#include "Components/Transform.h"
#include "ECS/Core.h"
#include "ECS/EntityHandle.h"

namespace Moonlight { class Texture; }

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

class PreviewResourceEvent
	: public Event<PreviewResourceEvent>
{
public:
	PreviewResourceEvent()
		: Event()
	{
	}
	SharedPtr<Moonlight::Texture> Subject;
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
	Transform* SelectedTransform = nullptr;
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