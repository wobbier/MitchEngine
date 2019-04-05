#include "PCH.h"
#include "ComponentFilter.h"

bool ComponentFilter::PassFilter(const ComponentTypeArray& InComponentTypeArray) const
{
	if ((InComponentTypeArray & RequiredComponentsList) != RequiredComponentsList)
	{
		return false;
	}

	if ((InComponentTypeArray & RequiresOneOfComponentsList).any())
	{
		return true;
	}

	// Exclude any components we don't want
	if ((ExcludeComponentsList & InComponentTypeArray).any())
	{
		return false;
	}

	return true;
}

void ComponentFilter::Clear()
{
	RequiredComponentsList.reset();
	RequiresOneOfComponentsList.reset();
	ExcludeComponentsList.reset();
}