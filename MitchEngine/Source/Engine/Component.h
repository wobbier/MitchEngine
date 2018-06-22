// 2015 Mitchell Andrews
#pragma once
#include <string>
#include <vector>
#include "ClassTypeId.h"

class Entity;

class BaseComponent
{
	friend class ComponentStorage;
public:
	~BaseComponent() = default;

	virtual void Init() = 0;
	const Entity* GetParentEntity();

private:
	void OnCreate(Entity* Owner);

	Entity* ParentEntity;
};

template<typename T>
class Component
	: public BaseComponent
{
public:
	static TypeId GetTypeId()
	{
		return ClassTypeId<BaseComponent>::GetTypeId<T>();
	}

	// Each core must update each loop
	virtual void Update(float dt)
	{
	}
private:
};

typedef std::vector<std::reference_wrapper<BaseComponent>> ComponentArray;
