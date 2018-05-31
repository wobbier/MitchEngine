#include "ComponentFilter.h"

using namespace MAN;

bool ComponentFilter::PassFilter(const ComponentTypeArray& InComponentTypeArray) const
{
	// Loop through the component type bits
	std::size_t Index = RequiredComponentsList.find_first();
	for (; Index != ComponentTypeArray::npos; Index = RequiredComponentsList.find_next(Index))
	{
		// Check to see if all of the required components are attached to meet the requirements
		if (Index >= InComponentTypeArray.size() || InComponentTypeArray[Index] == false)
		{
			return false;
		}
	}

	if (!RequiresOneOfComponentsList.empty())
	{
		if (RequiresOneOfComponentsList.intersects(InComponentTypeArray))
		{
			return false;
		}
	}

	// Check for clashing components
	if (!ExcludeComponentsList.empty())
	{
		if (ExcludeComponentsList.intersects(InComponentTypeArray))
		{
			return false;
		}
	}
	return true;
}

void ComponentFilter::Clear()
{
	RequiredComponentsList.clear();
	RequiresOneOfComponentsList.clear();
	ExcludeComponentsList.clear();
}