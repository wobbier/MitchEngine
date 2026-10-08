#include "Events/EventManager.h"
#include "Events/EventReceiver.h"
#include "optick.h"
#include "Dementia.h"
#include "Core/Assert.h"
#include <algorithm>
#include <atomic>

namespace
{
    std::atomic<bool> s_isAlive{ false };
}


EventManager::EventManager()
    : m_mainThread( std::this_thread::get_id() )
{
    s_isAlive.store( true );
}


EventManager::~EventManager()
{
    s_isAlive.store( false );
}


bool EventManager::IsAlive()
{
    return s_isAlive.load();
}


void EventManager::RegisterReceiver( EventReceiver* receiver, std::vector<TypeId> events )
{
    for( auto type : events )
    {
        std::vector<EventReceiver*>& receivers = m_eventReceivers[type];
        if( std::find( receivers.begin(), receivers.end(), receiver ) == receivers.end() )
        {
            receivers.push_back( receiver );
        }
    }
}


void EventManager::DeRegisterReciever( EventReceiver* receiver )
{
    for( auto& mapEntry : m_eventReceivers )
    {
        auto& vec = mapEntry.second;
        if( m_dispatchDepth > 0 )
        {
            // Mid-dispatch: null the slot so in-flight iteration skips it; compact afterwards.
            for( EventReceiver*& entry : vec )
            {
                if( entry == receiver )
                {
                    entry = nullptr;
                    m_needsCompaction = true;
                }
            }
        }
        else
        {
            vec.erase( std::remove( vec.begin(), vec.end(), receiver ), vec.end() );
        }
    }
}


void EventManager::Compact( std::vector<EventReceiver*>& receivers )
{
    receivers.erase( std::remove( receivers.begin(), receivers.end(), nullptr ), receivers.end() );
}


void EventManager::FireEvent( TypeId eventId, const BaseEvent& event )
{
    ME_ASSERT_MSG( std::this_thread::get_id() == m_mainThread, "Fire events on the main thread; use Queue() from other threads." );

    auto found = m_eventReceivers.find( eventId );
    if( found == m_eventReceivers.end() )
    {
        return;
    }

    ++m_dispatchDepth;
    // Index-based: receivers registered during dispatch are appended and also see this event.
    std::vector<EventReceiver*>& receivers = found->second;
    for( size_t i = 0; i < receivers.size(); ++i )
    {
        EventReceiver* receiver = receivers[i];
        if( receiver && receiver->OnEvent( event ) )
        {
            break;
        }
    }
    --m_dispatchDepth;

    if( m_dispatchDepth == 0 && m_needsCompaction )
    {
        for( auto& entry : m_eventReceivers )
        {
            Compact( entry.second );
        }
        m_needsCompaction = false;
    }
}


void EventManager::QueueEvent( std::unique_ptr<BaseEvent> event )
{
    std::lock_guard<std::mutex> lock( m_queueMutex );
    m_queuedEvents.push_back( std::move( event ) );
}


void EventManager::FirePendingEvents()
{
    OPTICK_EVENT( "EventManager::FirePendingEvents" );
    std::vector<std::unique_ptr<BaseEvent>> events;
    {
        std::lock_guard<std::mutex> lock( m_queueMutex );
        events.swap( m_queuedEvents );
    }
    // Events queued while these are handled wait for the next frame.
    for( std::unique_ptr<BaseEvent>& event : events )
    {
        FireEvent( event->GetEventId(), *event );
    }
}
