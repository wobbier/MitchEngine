#pragma once
#include <unordered_map>
#include <queue>

#include "Singleton.h"
#include "ClassTypeId.h"
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
	void QueueEvent(const BaseEvent& event);
	void FirePendingEvents();

protected:
	std::unordered_map<TypeId, std::vector<EventReceiver*>> m_eventReceivers;

	std::queue<BaseEvent> m_queuedEvents;
};