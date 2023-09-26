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

    void Fire();
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
    EventManager::GetInstance().QueueEvent( std::move( *this ) );
}

template <class T>
void Event<T>::Fire()
{
    EventManager::GetInstance().FireEvent( GetEventId(), *this );
}
