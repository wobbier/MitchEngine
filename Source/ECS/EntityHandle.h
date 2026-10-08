#pragma once
#include "EntityID.h"
#include <type_traits>
#include "Pointers.h"
#include "JSON.h"
#include "Reflection/Reflection.h"

class World;
class Entity;

struct EntityIDHash
{
    std::size_t operator()( const EntityID& k ) const
    {
        return std::hash<EntityID::IntType>()( k.Value() );
    }
};

// Weak reference to an entity. Cheap to copy; resolves to null once the entity is destroyed
// (the id's generation no longer matches its slot). The World must outlive its handles.
class EntityHandle
{
public:
    EntityHandle() = default;
    EntityHandle( EntityID InID, World* InWorld );
    EntityHandle( EntityID InID, const WeakPtr<World>& InWorld );
    EntityHandle( EntityID InID, const SharedPtr<World>& InWorld );

    explicit operator bool() const;
    bool operator ==( const EntityHandle& other ) const;
    bool operator !=( const EntityHandle& other ) const { return !( *this == other ); }
    Entity* operator->() const;

    Entity* Get() const;
    bool IsValid() const;

    EntityID GetID() const;
    World* GetWorld() const { return GameWorld; }

    void Reset();

private:
    EntityID ID;
    World* GameWorld = nullptr;
};

// While alive, EntityHandle fields deserialized through reflection resolve their GUIDs in this world.
class SerializationWorldScope
{
public:
    explicit SerializationWorldScope( World* InWorld );
    ~SerializationWorldScope();
    SerializationWorldScope( const SerializationWorldScope& ) = delete;
    SerializationWorldScope& operator=( const SerializationWorldScope& ) = delete;

    static World* GetCurrent();

private:
    World* m_previous = nullptr;
};

// Entity references serialize by GUID so they survive save/load and prefab instancing.
namespace Reflection
{
    template<>
    struct CustomTypeTraits<EntityHandle>
    {
        static constexpr bool IsCustom = true;
        static constexpr const char* Name = "EntityHandle";
        static void ToJson( const EntityHandle& value, json& out );
        static bool FromJson( EntityHandle& value, const json& in );
    };
}
