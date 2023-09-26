#pragma once
#include "Events/EventManager.h"
#include "Events/Event.h"
#include "Math/Vector2.h"

class WindowResizedEvent
    : public Event<WindowResizedEvent>
{
public:
    Vector2 NewSize;
};
