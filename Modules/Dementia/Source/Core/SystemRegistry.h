#pragma once
#include "ISystem.h"
#include <vector>

class SystemRegistry
{
public:
    SystemRegistry() = default;
    ~SystemRegistry();

    void RegisterSystem( ISystem* inSystem );
    void UnRegisterSystem( ISystem* inSystem );

    template<typename T>
    inline T* GetSystem() const
    {
        for( auto system : Systems )
        {
            if( system->GetSystemID() == T::sSystemID )
            {
                return static_cast<T*>( system );
            }
        }

        return nullptr;
    }

private:
    std::vector<ISystem*> Systems;
};