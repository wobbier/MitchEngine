#include "PCH.h"
#include "EventManager.h"

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