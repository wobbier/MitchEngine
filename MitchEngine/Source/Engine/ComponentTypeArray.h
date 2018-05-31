// 2015 Mitchell Andrews

#pragma once
#include <boost/dynamic_bitset.hpp>

namespace MAN
{
	// A DYNAMIC bitset used to check whether we have a
	// component attached to the Entity.
	// We use the Component's Type as the index, and if
	// the bit is true at the Type index we have the component.
	typedef boost::dynamic_bitset<> ComponentTypeArray;

	template <class TContainer>
	void CheckCapacity(TContainer& InContainer, typename TContainer::size_type InIndex)
	{
		if (InContainer.size() <= InIndex)
		{
			InContainer.resize(InIndex + 1);
		}
	}
}