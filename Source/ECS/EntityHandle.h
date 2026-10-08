#pragma once
#include "EntityID.h"
#include <type_traits>
#include "Pointers.h"
#include "JSON.h"
#include "Reflection/Reflection.h"
#include <unordered_map>

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

// While alive, EntityHandle fields deserialized through reflection resolve their GUIDs in this world,
// optionally translating saved GUIDs through a remap table (prefab instances, paste, duplicate).
class SerializationWorldScope
{
public:
    explicit SerializationWorldScope( World* InWorld, const std::unordered_map<uint64_t, uint64_t>* InRemap = nullptr );
    ~SerializationWorldScope();
    SerializationWorldScope( const SerializationWorldScope& ) = delete;
    SerializationWorldScope& operator=( const SerializationWorldScope& ) = delete;

    static World* GetCurrent();
    // Saved GUID -> live entity in the current scope's world (null handle if unknown).
    static EntityHandle Resolve( uint64_t InSavedGUID );

private:
    World* m_previousWorld = nullptr;
    const std::unordered_map<uint64_t, uint64_t>* m_previousRemap = nullptr;
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
