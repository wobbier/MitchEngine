#include "Events/EventManager.h"
#include "Events/EventReceiver.h"

EventManager::EventManager()
{

}

void EventManager::RegisterReceiver(EventReceiver* receiver, std::vector<TypeId> events)
{
	for (auto type : events)
	{
		m_eventReceivers[type].push_back(receiver);
	}
}

void EventManager::FireEvent(TypeId eventId, const BaseEvent& event)
{
	auto& recievers = m_eventReceivers[eventId];
	for (auto reciever : recievers)
	{
		if (reciever->OnEvent(event))
		{
			return;
		}
	}
}

void EventManager::QueueEvent(BaseEvent& event)
{
	m_queuedEvents.push(std::move(event));
}

void EventManager::FirePendingEvents()
{
	while (!m_queuedEvents.empty())
	{
		const BaseEvent& event = m_queuedEvents.front();
		m_queuedEvents.pop();
		auto& recievers = m_eventReceivers[event.GetEventId()];
		for (auto reciever : recievers)
		{
			if (reciever->OnEvent(event))
			{
				break;
			}
		}
	}
}