#pragma once
#include "ClassTypeId.h"
#include "EventManager.h"

template <class T>
class Event
    : public BaseEvent
{
public:
    Event();

    static TypeId GetEventId();

    // Dispatch now, on this thread (main thread).
    void Fire();
    // Copy the event and deliver it on the main thread at the start of the next frame. Thread safe.
    void Queue();
};

template <class T>
TypeId Event<T>::GetEventId()
{
    return ClassTypeId<BaseEvent>::GetTypeId<T>();
}

template <class T>
Event<T>::Event() : BaseEvent( GetEventId() )
{

}

template <class T>
void Event<T>::Queue()
{
    EventManager::GetInstance().QueueEvent( std::make_unique<T>( static_cast<const T&>( *this ) ) );
}

template <class T>
void Event<T>::Fire()
{
    EventManager::GetInstance().FireEvent( GetEventId(), *this );
}
