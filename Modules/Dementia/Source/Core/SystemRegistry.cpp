#include "SystemRegistry.h"
#include <algorithm>

SystemRegistry::~SystemRegistry()
{
}

void SystemRegistry::RegisterSystem( ISystem* inSystem )
{
    Systems.push_back( inSystem );
}

void SystemRegistry::UnRegisterSystem( ISystem* inSystem )
{
    auto it = std::find( Systems.begin(), Systems.end(), inSystem );
    if( it != Systems.end() )
    {
        Systems.erase( it );
    }
}