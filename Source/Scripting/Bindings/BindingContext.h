#pragma once
#include "ECS/EntityHandle.h"
#include "ECS/Entity.h"
#include "ECS/EntityID.h"
#include "Pointers.h"
#include <functional>
#include <string>

class World;

class PhysicsCore;
class Physics2DCore;
class NavigationCore;

namespace ScriptBindings
{
    // The world script calls act on: the engine's game world (SetWorld), or, inside a WorldScope
    // on this thread, the scope's (NET-0: a server or a rollback simulation stepping its own
    // world runs its scripts against it).
    void SetWorld( WeakPtr<World> inWorld );
    WeakPtr<World> GetWorld();

    class WorldScope
    {
    public:
        explicit WorldScope( WeakPtr<World> InWorld );
        ~WorldScope();
        WorldScope( const WorldScope& ) = delete;
        WorldScope& operator=( const WorldScope& ) = delete;

    private:
        WeakPtr<World> m_previous;
        bool m_hadPrevious = false;
    };

    // The script world's cores (null when it has none): bindings use these rather than the
    // engine's, so a scoped world queries its own physics and navigation.
    PhysicsCore* Physics();
    Physics2DCore* Physics2D();
    NavigationCore* Navigation();

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
    // Whether the entity has a component of that type (any registered component).
    bool HasComponentOfType( const Entity& InEntity, TypeId InType );
}
