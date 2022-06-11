#include "SystemRegistry.h"

SystemRegistry::~SystemRegistry()
{

}

void SystemRegistry::RegisterSystem(ISystem* inSystem)
{
    Systems.push_back(inSystem);
}

void SystemRegistry::UnRegisterSystem(ISystem* inSystem)
{
    Systems.remove(inSystem);
}

