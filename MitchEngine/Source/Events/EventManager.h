#pragma once
#include <unordered_map>

#include "Utility/Singleton.h"
#include "Utility/ClassTypeId.h"
#include "EventReceiver.h"

class EventManager
	: public Singleton<EventManager>
{
	friend class Singleton<EventManager>;
private:
	EventManager();

public:
	void RegisterReceiver(EventReceiver* receiver, std::vector<TypeId> events);

	void FireEvent(TypeId eventId, const BaseEvent& event);

protected:
	std::unordered_map<TypeId, std::vector<EventReceiver*>> m_eventReceivers;
};