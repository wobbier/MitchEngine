#pragma once
#include "ECS/EntityHandle.h"
#include "ECS/Entity.h"
#include "ECS/EntityID.h"
#include "Pointers.h"
#include <functional>
#include <string>

class World;

namespace ScriptBindings
{
    void SetWorld( WeakPtr<World> inWorld );
    WeakPtr<World> GetWorld();

    EntityHandle MakeHandle( EntityID inId );

    void RegisterComponentOps( const std::string& inName, std::function<bool( EntityHandle )> inHas, std::function<void( EntityHandle )> inAdd );

    template<typename T>
    void RegisterComponent( const std::string& inName )
    {
        RegisterComponentOps( inName,
            []( EntityHandle e ) { return e->HasComponent<T>(); },
            []( EntityHandle e ) { e->AddComponent<T>(); } );
    }

    bool HasComponentNamed( EntityHandle inHandle, const char* inName );
    bool AddComponentNamed( EntityHandle inHandle, const char* inName );
}
