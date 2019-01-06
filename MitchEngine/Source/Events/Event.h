#pragma once
#include "Utility/ClassTypeId.h"

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
		: BaseEvent(ClassTypeId<BaseEvent>::GetTypeId<T>())
	{

	}
	static TypeId GetEventId()
	{
		return ClassTypeId<BaseEvent>::GetTypeId<T>();
	}
	void Fire();
};

template <class T>
void Event<T>::Fire()
{
	EventManager::GetInstance().FireEvent(GetEventId(), *this);
}
