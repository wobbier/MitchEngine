#pragma once
#include "Event.h"

class EventReceiver
{
public:
	EventReceiver();
	~EventReceiver();

	virtual bool OnEvent(const BaseEvent& evt) = 0;
};