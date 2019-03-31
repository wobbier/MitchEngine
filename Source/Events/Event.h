#pragma once
#include "ClassTypeId.h"

class BaseEvent
{
public:
	TypeId GetEventId() const
	{
		return id;
	}
protected:
	BaseEvent(TypeId type)
		: id(type)
	{

	}
private:
	TypeId id;
};

template <class T>
class Event
	: public BaseEvent
{
public:
	Event()
		: BaseEvent(GetEventId())
	{
	}

	static TypeId GetEventId()
	{
		return ClassTypeId<BaseEvent>::GetTypeId<T>();
	}

	void Fire();
	void Queue();
};

template <class T>
void Event<T>::Queue()
{
	EventManager::GetInstance().QueueEvent(*this);
}

template <class T>
void Event<T>::Fire()
{
	EventManager::GetInstance().FireEvent(GetEventId(), *this);
}
