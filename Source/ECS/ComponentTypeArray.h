// 2018 Mitchell Andrews

#pragma once
#include <bitset>

typedef std::bitset<64> ComponentTypeArray;

template <class TContainer>
void CheckCapacity( TContainer& InContainer, typename TContainer::size_type InIndex )
{
    if( InContainer.size() <= InIndex )
    {
        InContainer.resize( InIndex + 1 );
    }
}