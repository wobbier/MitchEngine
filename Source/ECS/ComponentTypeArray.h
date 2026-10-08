// 2018 Mitchell Andrews

#pragma once
#include <bitset>
#include <cstddef>

// Upper bound on distinct component types (engine + game). Raise freely; it only sizes the masks.
static constexpr std::size_t kMaxComponentTypes = 256;

typedef std::bitset<kMaxComponentTypes> ComponentTypeArray;

template <class TContainer>
void CheckCapacity( TContainer& InContainer, typename TContainer::size_type InIndex )
{
    if( InContainer.size() <= InIndex )
    {
        InContainer.resize( InIndex + 1 );
    }
}
