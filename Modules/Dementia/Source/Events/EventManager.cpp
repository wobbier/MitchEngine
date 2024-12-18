#include "Events/EventManager.h"
#include "Events/EventReceiver.h"
#include "optick.h"
#include "Dementia.h"

EventManager::EventManager()
{
}

void EventManager::RegisterReceiver( EventReceiver* receiver, std::vector<TypeId> events )
{
    for( auto type : events )
    {
        m_eventReceivers[type].push_back( receiver );
    }
}

void EventManager::DeRegisterReciever( EventReceiver* receiver )
{
    for( auto& mapEntry : m_eventReceivers )
    {
        auto& vec = mapEntry.second;
        vec.erase( std::remove( vec.begin(), vec.end(), receiver ), vec.end() );
    }
}

void EventManager::FireEvent( TypeId eventId, const BaseEvent& event )
{
    auto& recievers = m_eventReceivers[eventId];
    for( auto reciever : recievers )
    {
        if( reciever->OnEvent( event ) )
        {
            return;
        }
    }
}

void EventManager::QueueEvent( SharedPtr<BaseEvent> event )
{
    m_queuedEvents.push( event );
}

void EventManager::FirePendingEvents()
{
    OPTICK_EVENT( "EventManager::FirePendingEvents" );
    while( !m_queuedEvents.empty() )
    {
        SharedPtr<BaseEvent> event = m_queuedEvents.front();
        m_queuedEvents.pop();
        auto& recievers = m_eventReceivers[event->GetEventId()];
        for( auto reciever : recievers )
        {
            if( reciever->OnEvent( *event.get() ) )
            {
                break;
            }
        }
    }
}