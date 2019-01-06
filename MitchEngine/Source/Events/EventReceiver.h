#pragma once
#include "Event.h"

class EventReceiver
{
public:
	EventReceiver() = default;
	~EventReceiver() = default;

	virtual bool OnEvent(const BaseEvent& evt) = 0;
};