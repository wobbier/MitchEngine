#pragma once
#include "Component.h"
#include "ComponentTypeArray.h"
#include "ClassTypeId.h"

class ComponentFilter
{
public:
	ComponentFilter() = default;

	~ComponentFilter() = default;

	template<typename C>
	ComponentFilter& Requires()
	{
		static_assert(std::is_base_of<BaseComponent, C>(), "C doesn't inherit from Component");
		RequiredComponentsList[C::GetTypeId()] = true;
		return *this;
	}
	template<typename C>
	ComponentFilter& RequiresOneOf()
	{
		static_assert(std::is_base_of<BaseComponent, C>(), "C doesn't inherit from Component");
		RequiresOneOfComponentsList[C::GetTypeId()] = true;
		return *this;
	}
	template<typename C>
	ComponentFilter& Excludes()
	{
		static_assert(std::is_base_of<BaseComponent, C>(), "C doesn't inherit from Component");
		ExcludeComponentsList[C::GetTypeId()] = true;
		return *this;
	}
	bool PassFilter(const ComponentTypeArray& InComponentTypeArray) const;

	void Clear();

private:
	ComponentTypeArray RequiredComponentsList;

	ComponentTypeArray RequiresOneOfComponentsList;

	ComponentTypeArray ExcludeComponentsList;
};
