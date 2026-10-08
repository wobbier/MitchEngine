#pragma once
#include "Dementia.h"
#include "ECS/Entity.h"
#include "ECS/EntityHandle.h"
#include "ECS/ComponentPool.h"
#include "ECS/ComponentTypeArray.h"
#include "Resource/ResourceCache.h"
#include "Pointers.h"
#include <JSON.h>
#include <deque>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class Transform;
class BaseCore;
class UpdateContext;

// Owns entities, their components and the cores (systems) that process them.
//
// Structural changes (component add/remove, activation, destruction) are applied to cores at sync
// points - World::Simulate() - so cores never see their entity lists mutate mid-iteration.
// Component *addition* is immediate (GetComponent works right away); removal and destruction are
// deferred to the next sync point.
class World
    : public std::enable_shared_from_this<World>
{
public:
    typedef std::unordered_map<TypeId, BaseCore*> CoreArray;

    World();
    ~World();
    ME_HARDSTUCK( World );

    SharedPtr<World> GetSharedPtr();

    // ------------------------------------------------------------------ Entities
    EntityHandle CreateEntity( const std::string& InName = "" );
    // Creates an entity that will be assigned the given GUID (scene/prefab loading).
    EntityHandle CreateEntityWithGUID( uint64_t InGUID, const std::string& InName = "" );

    void MarkEntityForDelete( Entity& EntityToDestroy );
    void DestroyEntity( const EntityHandle& InEntity );

    std::size_t GetEntityCount() const;
    EntityHandle GetEntity( const EntityID& id );
    Entity* GetEntityRaw( const EntityID& id );
    EntityHandle FindEntityByIDValue( uint64_t id );
    EntityHandle FindEntityByGUID( uint64_t InGUID );
    // First entity with this name (linear scan).
    EntityHandle FindEntityByName( const std::string& InName );
    const bool EntityExists( const EntityID& InEntity ) const;
    bool IsActive( Entity& InEntity );

    // Calls fn( Entity& ) for every live entity.
    template<typename Fn>
    void ForEachEntity( Fn&& fn );

    // Calls fn( Entity&, A&, B&... ) for every live, active entity that has all the components
    // (enabled). Structural changes made inside fn are deferred until the next sync point.
    template<typename First, typename... Rest, typename Fn>
    void Each( Fn&& fn );

    // ------------------------------------------------------------------ Components
    template<typename T>
    ComponentPool<T>& GetComponentPool();
    IComponentPool* GetComponentPool( TypeId InTypeId ) const;

    template<typename T, typename... Args>
    T& AddComponent( Entity& InEntity, Args&&... args );

    // ------------------------------------------------------------------ Simulation
    // Sync point: applies pending activation changes, core membership, component removals and
    // entity destruction.
    void Simulate();
    void Start();
    void Stop();
    void Destroy();
    void Unload();
    void UpdateLoadedCores( const UpdateContext& inUpdateContext );
    void FixedUpdateLoadedCores( const UpdateContext& inUpdateContext );
    void LateUpdateLoadedCores( const UpdateContext& inUpdateContext );

    bool IsLoading = true;

    // ------------------------------------------------------------------ Cores
    template <typename TCore>
    void AddCore( TCore& inCore );

    template <typename TCore>
    bool HasCore();

    template <typename TCore>
    TCore* GetCore();

    BaseCore* GetCore( TypeId InType );
    bool HasCore( TypeId InType );
    BaseCore* AddCoreByName( const std::string& core );

    // All cores, sorted by priority.
    std::vector<BaseCore*> GetAllCores();
    const CoreArray& GetAllCoresArray();

    // Cores created from scenes (updated by UpdateLoadedCores), sorted by priority.
    const std::vector<BaseCore*>& GetLoadedCores() const { return m_loadedCores; }

    // ------------------------------------------------------------------ Prefabs
    EntityHandle CreateFromPrefab( const std::string& FilePath, Transform* Parent = nullptr );

    // ------------------------------------------------------------------ Internal (Entity facade)
    struct EntityRecord
    {
        Entity Facade;
        uint32_t Generation = 1;
        bool Alive = false;
        bool ActiveSelf = true;
        bool ActiveInHierarchy = false;
        bool IsLoading = false;
        bool DestroyOnLoad = true;
        bool PendingDestroy = false;
        bool QueuedForSync = false;
        uint8_t Layer = 0;
        uint64_t GUID = 0;
        std::string Name;
        // Components present on the entity.
        ComponentTypeArray Mask;
        // Components present and enabled.
        ComponentTypeArray EnabledMask;
        // Components queued for removal at the next sync point.
        ComponentTypeArray PendingRemoval;
    };

    EntityRecord* GetRecord( const EntityID& InId );
    const EntityRecord* GetRecord( const EntityID& InId ) const;

    // Queues the entity (and, when activity may change, its children) for re-evaluation at the
    // next sync point.
    void MarkEntityDirty( const EntityID& InId );

    BaseComponent* GetComponent( const EntityID& InId, TypeId InTypeId ) const;
    void RemoveComponent( const EntityID& InId, TypeId InTypeId );
    void OnComponentEnabledChanged( BaseComponent& InComponent );

    // Builds a new 64-bit random GUID (never 0).
    static uint64_t GenerateGUID();

private:
    void AddCore( BaseCore& InCore, TypeId InCoreTypeId, bool OwnedByWorld );
    void SortCores();
    EntityHandle CreateEntityInternal( uint64_t InGUID, const std::string& InName );
    void SyncEntity( uint32_t InIndex );
    void ApplyPendingRemovals( uint32_t InIndex );
    void DestroyEntityNow( const EntityID& InId );
    void CollectHierarchy( uint32_t InIndex, std::vector<EntityID>& OutIds );
    bool ComputeActiveInHierarchy( uint32_t InIndex ) const;
    void RemoveCore( TypeId InType );

    EntityHandle LoadPrefab( const json& obj, Transform* parent, Transform* root );

    std::deque<EntityRecord> m_records;
    std::vector<uint32_t> m_freeIndices;
    std::size_t m_aliveCount = 0;
    std::unordered_map<uint64_t, uint32_t> m_guidToIndex;

    std::vector<std::unique_ptr<IComponentPool>> m_pools;   // indexed by component TypeId

    CoreArray Cores;
    std::vector<BaseCore*> m_sortedCores;
    std::vector<BaseCore*> m_loadedCores;
    std::vector<std::unique_ptr<BaseCore>> m_ownedCores;

    std::vector<uint32_t> m_syncQueue;
    std::vector<uint32_t> m_destroyQueue;
    int m_iterationDepth = 0;
};

// ---------------------------------------------------------------------------------------------
// World templates
// ---------------------------------------------------------------------------------------------

template<typename T>
ComponentPool<T>& World::GetComponentPool()
{
    const TypeId typeId = T::GetStaticTypeId();
    ME_ASSERT_MSG( typeId < kMaxComponentTypes, "Too many component types: raise kMaxComponentTypes." );
    if( typeId >= m_pools.size() )
    {
        m_pools.resize( typeId + 1 );
    }
    if( !m_pools[typeId] )
    {
        m_pools[typeId] = std::make_unique<ComponentPool<T>>();
    }
    return static_cast<ComponentPool<T>&>( *m_pools[typeId] );
}


template<typename T, typename... Args>
T& World::AddComponent( Entity& InEntity, Args&&... args )
{
    EntityRecord* record = GetRecord( InEntity.GetId() );
    ME_ASSERT_MSG( record, "Adding a component to a dead entity." );

    ComponentPool<T>& pool = GetComponentPool<T>();
    const uint32_t index = InEntity.GetId().Index;
    if( T* existing = pool.Find( index ) )
    {
        // Re-adding a component that is queued for removal revives it.
        record->PendingRemoval.reset( T::GetStaticTypeId() );
        return *existing;
    }

    T& component = pool.Emplace( index, std::forward<Args>( args )... );
    component.Parent = EntityHandle( InEntity.GetId(), this );

    const TypeId typeId = T::GetStaticTypeId();
    record->Mask.set( typeId );
    if( component.IsEnabled() )
    {
        record->EnabledMask.set( typeId );
    }

    if( !record->IsLoading )
    {
        component.Init();
        if( record->ActiveInHierarchy && component.IsEnabled() )
        {
            component.OnEnable();
        }
    }
    MarkEntityDirty( InEntity.GetId() );
    return component;
}


template<typename TCore>
void World::AddCore( TCore& InCore )
{
    AddCore( InCore, TCore::GetTypeId(), false );
}


template<typename TCore>
bool World::HasCore()
{
    return HasCore( TCore::GetTypeId() );
}


template<typename TCore>
TCore* World::GetCore()
{
    return static_cast<TCore*>( GetCore( TCore::GetTypeId() ) );
}


template<typename Fn>
void World::ForEachEntity( Fn&& fn )
{
    ++m_iterationDepth;
    for( EntityRecord& record : m_records )
    {
        if( record.Alive )
        {
            fn( record.Facade );
        }
    }
    --m_iterationDepth;
}


template<typename First, typename... Rest, typename Fn>
void World::Each( Fn&& fn )
{
    ComponentTypeArray required;
    required.set( First::GetStaticTypeId() );
    ( required.set( Rest::GetStaticTypeId() ), ... );

    ComponentPool<First>& pool = GetComponentPool<First>();
    ++m_iterationDepth;
    // Iterate a snapshot of the entries: components added inside fn must not invalidate iteration.
    const std::vector<IComponentPool::Entry> entries = pool.GetEntries();
    for( const IComponentPool::Entry& entry : entries )
    {
        EntityRecord& record = m_records[entry.EntityIndex];
        if( !record.Alive || !record.ActiveInHierarchy || ( record.EnabledMask & required ) != required )
        {
            continue;
        }
        fn( record.Facade, *static_cast<First*>( entry.Component ), *GetComponentPool<Rest>().Find( entry.EntityIndex )... );
    }
    --m_iterationDepth;
}

// ---------------------------------------------------------------------------------------------
// Entity template members (need the complete World)
// ---------------------------------------------------------------------------------------------

template <typename T>
bool Entity::HasComponent() const
{
    static_assert( std::is_base_of<BaseComponent, T>(), "T is not a component" );
    return HasComponent( T::GetStaticTypeId() );
}


template <typename T, typename... Args>
T& Entity::AddComponent( Args&&... args )
{
    static_assert( std::is_base_of<BaseComponent, T>(), "T is not a component, cannot add T to entity" );
    return GameWorld->AddComponent<T>( *const_cast<Entity*>( this ), std::forward<Args>( args )... );
}


template <typename T>
T& Entity::GetComponent() const
{
    static_assert( std::is_base_of<BaseComponent, T>(), "T is not a component, cannot get T from Entity" );
    T* component = GameWorld->GetComponentPool<T>().Find( Id.Index );
    ME_ASSERT_MSG( component, "Entity does not have the requested component." );
    return *component;
}


template <typename T>
T* Entity::TryGetComponent() const
{
    static_assert( std::is_base_of<BaseComponent, T>(), "T is not a component" );
    return IsValid() ? GameWorld->GetComponentPool<T>().Find( Id.Index ) : nullptr;
}


template <typename T>
void Entity::RemoveComponent()
{
    RemoveComponent( T::GetStaticTypeId() );
}

// Most users of World also use cores; included last because Core.h -> Entity.h -> World.h.
#include "ECS/Core.h"
