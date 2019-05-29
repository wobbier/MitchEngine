#pragma once
#include "Event.h"

class EventReceiver
{
public:
	virtual bool OnEvent(const BaseEvent& evt) = 0;
};