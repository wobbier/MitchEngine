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

	EntityID Parent;

#if ME_EDITOR
	virtual void OnEditorInspect() = 0;
#endif
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

#if ME_EDITOR
	virtual void OnEditorInspect() override
	{
	}
#endif
};

using ComponentArray = std::vector<std::reference_wrapper<BaseComponent>>;
