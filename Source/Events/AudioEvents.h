#pragma once
#include "Events/EventManager.h"
#include "Events/Event.h"

class PlayAudioEvent
	: public Event<PlayAudioEvent>
{
public:
	PlayAudioEvent() = default;
	PlayAudioEvent(const std::string& InSource)
		: SourceName(InSource)
	{
	}
	std::string SourceName;
};