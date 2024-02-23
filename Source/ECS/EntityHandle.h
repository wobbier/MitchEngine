#pragma once
#include "EntityID.h"
#include <type_traits>
#include "Pointers.h"

class World;
class Entity;

struct EntityIDHash
{
    std::size_t operator()( const EntityID& k ) const
    {
        return std::hash<EntityID::IntType>()( k.Value() );
    }
};

class EntityHandle
{
public:
    EntityHandle() = default;
    EntityHandle( EntityID InID, WeakPtr<World> InWorld );

    explicit operator bool() const;
    bool operator ==( const EntityHandle& other ) const;
    Entity* operator->() const;

    Entity* Get() const;

    EntityID GetID() const;

private:
    EntityID ID;
    WeakPtr<World> GameWorld;
};
