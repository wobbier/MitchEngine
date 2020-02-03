#pragma once
#include "Events/EventManager.h"
#include "Events/Event.h"

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

