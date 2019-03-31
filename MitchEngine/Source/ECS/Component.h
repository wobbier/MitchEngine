// 2018 Mitchell Andrews
#pragma once
#include <string>
#include <vector>
#include "ClassTypeId.h"
#include <memory>
#include "EntityID.h"

class BaseComponent
{
	friend class ComponentStorage;
public:
	BaseComponent() = default;
	~BaseComponent() = default;

	virtual void Init() = 0;

	virtual void OnEditorInspect() = 0;

	EntityID Parent;
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

	virtual void OnEditorInspect() override
	{
	}
private:
};

using ComponentArray = std::vector<std::reference_wrapper<BaseComponent>>;
