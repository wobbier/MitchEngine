// 2018 Mitchell Andrews
#pragma once
#include <string>
#include <vector>
#include "Utility/ClassTypeId.h"

class BaseComponent
{
	friend class ComponentStorage;
public:
	BaseComponent() = default;
	~BaseComponent() = default;

	virtual void Init() = 0;
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

using ComponentArray = std::vector<std::reference_wrapper<BaseComponent>>;
