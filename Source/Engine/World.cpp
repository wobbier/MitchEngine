#include "PCH.h"
#include "Profiling/FrameStats.h"
#include "Engine/World.h"
#include "ECS/Core.h"
#include "Components/Transform.h"
#include "Pointers.h"
#include "CLog.h"
#include "ECS/CoreDetail.h"
#include "ECS/ComponentDetail.h"
#include "File.h"
#include "Resources/JsonResource.h"
#include "World/SceneSerializer.h"
#include "optick.h"
#include "Core/Assert.h"
#include <algorithm>
#include <random>
#include <chrono>

namespace
{
    // Fires lifecycle callbacks for every enabled component of an entity.
    template<typename Fn>
    void ForEachEnabledComponent( const World::EntityRecord& record, World& world, Fn&& fn )
    {
        for( std::size_t typeId = 0; typeId < kMaxComponentTypes; ++typeId )
        {
            if( record.EnabledMask.test( typeId ) && !record.PendingRemoval.test( typeId ) )
            {
                if( BaseComponent* component = world.GetComponent( record.Facade.GetId(), typeId ) )
                {
                    fn( *component );
                }
            }
        }
    }
}


World::World()
{
}


World::~World()
{
    Destroy();
    for( auto& core : Cores )
    {
        core.second->Clear();
        core.second->GameWorld = nullptr;
    }
    Cores.clear();
    m_sortedCores.clear();
    m_loadedCores.clear();
    m_ownedCores.clear();
}


SharedPtr<World> World::GetSharedPtr()
{
    return shared_from_this();
}


uint64_t World::GenerateGUID()
{
    static thread_local std::mt19937_64 generator( std::random_device{}() ^ static_cast<uint64_t>( std::chrono::steady_clock::now().time_since_epoch().count() ) );
    uint64_t guid = 0;
    while( guid == 0 )
    {
        guid = generator();
    }
    return guid;
}

// ------------------------------------------------------------------------------------------------
// Entities
// ------------------------------------------------------------------------------------------------

EntityHandle World::CreateEntity( const std::string& InName )
{
    return CreateEntityInternal( 0, InName );
}


EntityHandle World::CreateEntityWithGUID( uint64_t InGUID, const std::string& InName )
{
    if( InGUID != 0 && m_guidToIndex.find( InGUID ) != m_guidToIndex.end() )
    {
        // Duplicate (e.g. the same prefab instanced twice): the caller remaps references.
        InGUID = 0;
    }
    return CreateEntityInternal( InGUID, InName );
}


EntityHandle World::CreateEntityInternal( uint64_t InGUID, const std::string& InName )
{
    uint32_t index;
    if( !m_freeIndices.empty() )
    {
        index = m_freeIndices.back();
        m_freeIndices.pop_back();
    }
    else
    {
        index = static_cast<uint32_t>( m_records.size() );
        m_records.emplace_back();
    }

    EntityRecord& record = m_records[index];
    const uint32_t generation = record.Generation;
    record = EntityRecord();
    record.Generation = generation;
    record.Alive = true;
    record.GUID = InGUID != 0 ? InGUID : GenerateGUID();
    record.Name = InName;

    const EntityID id( index, generation );
    record.Facade = Entity( *this, id );
    m_guidToIndex[record.GUID] = index;
    ++m_aliveCount;

    MarkEntityDirty( id );
    return EntityHandle( id, this );
}


World::EntityRecord* World::GetRecord( const EntityID& InId )
{
    if( InId.Index >= m_records.size() )
    {
        return nullptr;
    }
    EntityRecord& record = m_records[InId.Index];
    return ( record.Alive && record.Generation == InId.Generation ) ? &record : nullptr;
}


const World::EntityRecord* World::GetRecord( const EntityID& InId ) const
{
    if( InId.Index >= m_records.size() )
    {
        return nullptr;
    }
    const EntityRecord& record = m_records[InId.Index];
    return ( record.Alive && record.Generation == InId.Generation ) ? &record : nullptr;
}


void World::MarkEntityDirty( const EntityID& InId )
{
    EntityRecord* record = GetRecord( InId );
    if( !record || record->QueuedForSync )
    {
        return;
    }
    record->QueuedForSync = true;
    m_syncQueue.push_back( InId.Index );
}


void World::MarkEntityForDelete( Entity& EntityToDestroy )
{
    EntityRecord* record = GetRecord( EntityToDestroy.GetId() );
    if( !record || record->PendingDestroy )
    {
        return;
    }
    record->PendingDestroy = true;
    m_destroyQueue.push_back( EntityToDestroy.GetId().Index );
}


void World::DestroyEntity( const EntityHandle& InEntity )
{
    if( Entity* entity = InEntity.Get() )
    {
        MarkEntityForDelete( *entity );
    }
}


std::size_t World::GetEntityCount() const
{
    return m_aliveCount;
}


EntityHandle World::GetEntity( const EntityID& InEntity )
{
    return GetRecord( InEntity ) ? EntityHandle( InEntity, this ) : EntityHandle();
}


Entity* World::GetEntityRaw( const EntityID& InEntity )
{
    EntityRecord* record = GetRecord( InEntity );
    return record ? &record->Facade : nullptr;
}


EntityHandle World::FindEntityByIDValue( uint64_t id )
{
    return GetEntity( EntityID::FromValue( id ) );
}


EntityHandle World::FindEntityByGUID( uint64_t InGUID )
{
    auto it = m_guidToIndex.find( InGUID );
    if( it == m_guidToIndex.end() )
    {
        return {};
    }
    const EntityRecord& record = m_records[it->second];
    return EntityHandle( record.Facade.GetId(), this );
}


EntityHandle World::FindEntityByName( const std::string& InName )
{
    for( EntityRecord& record : m_records )
    {
        if( record.Alive && record.Name == InName )
        {
            return EntityHandle( record.Facade.GetId(), this );
        }
    }
    return {};
}


const bool World::EntityExists( const EntityID& InEntity ) const
{
    return GetRecord( InEntity ) != nullptr;
}


bool World::IsActive( Entity& InEntity )
{
    const EntityRecord* record = GetRecord( InEntity.GetId() );
    return record && record->ActiveInHierarchy;
}

// ------------------------------------------------------------------------------------------------
// Components
// ------------------------------------------------------------------------------------------------

IComponentPool* World::GetComponentPool( TypeId InTypeId ) const
{
    return InTypeId < m_pools.size() ? m_pools[InTypeId].get() : nullptr;
}


BaseComponent* World::GetComponent( const EntityID& InId, TypeId InTypeId ) const
{
    const EntityRecord* record = GetRecord( InId );
    if( !record || InTypeId >= m_pools.size() || !m_pools[InTypeId] )
    {
        return nullptr;
    }
    return m_pools[InTypeId]->Get( InId.Index );
}


void World::RemoveComponent( const EntityID& InId, TypeId InTypeId )
{
    EntityRecord* record = GetRecord( InId );
    if( !record || InTypeId >= kMaxComponentTypes || !record->Mask.test( InTypeId ) )
    {
        return;
    }
    record->PendingRemoval.set( InTypeId );
    MarkEntityDirty( InId );
}


void World::OnComponentEnabledChanged( BaseComponent& InComponent )
{
    EntityRecord* record = GetRecord( InComponent.Parent.GetID() );
    if( !record )
    {
        return;
    }

    const TypeId typeId = InComponent.GetTypeId();
    record->EnabledMask.set( typeId, InComponent.IsEnabled() );

    if( record->ActiveInHierarchy && !record->IsLoading )
    {
        if( InComponent.IsEnabled() )
        {
            InComponent.OnEnable();
        }
        else
        {
            InComponent.OnDisable();
        }
    }
    MarkEntityDirty( record->Facade.GetId() );
}


void BaseComponent::SetEnabled( bool InEnabled )
{
    if( m_isEnabled == InEnabled )
    {
        return;
    }
    m_isEnabled = InEnabled;
    if( World* world = Parent.GetWorld() )
    {
        world->OnComponentEnabledChanged( *this );
    }
}

// ------------------------------------------------------------------------------------------------
// Sync
// ------------------------------------------------------------------------------------------------

bool World::ComputeActiveInHierarchy( uint32_t InIndex ) const
{
    uint32_t index = InIndex;
    // Walk up the Transform hierarchy; any inactive ancestor deactivates the entity.
    for( int depth = 0; depth < 4096; ++depth )
    {
        const EntityRecord& record = m_records[index];
        if( !record.Alive || !record.ActiveSelf )
        {
            return false;
        }

        const IComponentPool* transformPool = GetComponentPool( Transform::GetStaticTypeId() );
        const Transform* transform = transformPool ? static_cast<const Transform*>( transformPool->Get( index ) ) : nullptr;
        const Transform* parent = transform ? transform->GetParentTransform() : nullptr;
        if( !parent )
        {
            return true;
        }
        index = parent->Parent.GetID().Index;
    }
    ME_ASSERT_MSG( false, "Transform hierarchy too deep (or cyclic)." );
    return false;
}


void World::SyncEntity( uint32_t InIndex )
{
    EntityRecord& record = m_records[InIndex];
    record.QueuedForSync = false;
    if( !record.Alive )
    {
        return;
    }

    const bool wasActive = record.ActiveInHierarchy;
    const bool isActive = ComputeActiveInHierarchy( InIndex );
    const ComponentTypeArray effectiveMask = isActive ? ( record.EnabledMask & ~record.PendingRemoval ) : ComponentTypeArray();

    // Core membership. Removal happens while the components still exist so cores can clean up.
    for( BaseCore* core : m_sortedCores )
    {
        const bool shouldBeMember = isActive && core->GetComponentFilter().PassFilter( effectiveMask );
        const bool isMember = core->Contains( InIndex );
        if( shouldBeMember && !isMember )
        {
            core->Add( record.Facade );
        }
        else if( !shouldBeMember && isMember )
        {
            core->Remove( record.Facade );
        }
    }

    if( wasActive != isActive )
    {
        record.ActiveInHierarchy = isActive;
        ForEachEnabledComponent( record, *this, [isActive]( BaseComponent& component ) {
            if( isActive )
            {
                component.OnEnable();
            }
            else
            {
                component.OnDisable();
            }
        } );

        // Children inherit activity.
        if( const IComponentPool* transformPool = GetComponentPool( Transform::GetStaticTypeId() ) )
        {
            if( const Transform* transform = static_cast<const Transform*>( transformPool->Get( InIndex ) ) )
            {
                for( Transform* child : transform->GetChildren() )
                {
                    MarkEntityDirty( child->Parent.GetID() );
                }
            }
        }
    }

    ApplyPendingRemovals( InIndex );
}


void World::ApplyPendingRemovals( uint32_t InIndex )
{
    EntityRecord& record = m_records[InIndex];
    if( record.PendingRemoval.none() )
    {
        return;
    }

    for( std::size_t typeId = 0; typeId < kMaxComponentTypes; ++typeId )
    {
        if( !record.PendingRemoval.test( typeId ) )
        {
            continue;
        }
        if( BaseComponent* component = GetComponent( record.Facade.GetId(), typeId ) )
        {
            if( record.ActiveInHierarchy && component->IsEnabled() )
            {
                component->OnDisable();
            }
            component->OnDestroy();
            m_pools[typeId]->Destroy( InIndex );
        }
        record.Mask.reset( typeId );
        record.EnabledMask.reset( typeId );
    }
    record.PendingRemoval.reset();
}


void World::CollectHierarchy( uint32_t InIndex, std::vector<EntityID>& OutIds )
{
    // Children first, so a parent outlives everything below it during destruction.
    if( const IComponentPool* transformPool = GetComponentPool( Transform::GetStaticTypeId() ) )
    {
        if( const Transform* transform = static_cast<const Transform*>( transformPool->Get( InIndex ) ) )
        {
            for( Transform* child : transform->GetChildren() )
            {
                CollectHierarchy( child->Parent.GetID().Index, OutIds );
            }
        }
    }
    OutIds.push_back( m_records[InIndex].Facade.GetId() );
}


void World::DestroyEntityNow( const EntityID& InId )
{
    // Generation-checked: an OnDestroy callback may have created an entity in a freed slot.
    if( !GetRecord( InId ) )
    {
        return;
    }
    const uint32_t InIndex = InId.Index;
    EntityRecord& record = m_records[InIndex];

    for( BaseCore* core : m_sortedCores )
    {
        if( core->Contains( InIndex ) )
        {
            core->Remove( record.Facade );
            core->OnEntityDestroyed( record.Facade );
        }
    }

    for( std::size_t typeId = 0; typeId < kMaxComponentTypes; ++typeId )
    {
        if( !record.Mask.test( typeId ) )
        {
            continue;
        }
        if( BaseComponent* component = m_pools[typeId] ? m_pools[typeId]->Get( InIndex ) : nullptr )
        {
            if( record.ActiveInHierarchy && component->IsEnabled() )
            {
                component->OnDisable();
            }
            component->OnDestroy();
            m_pools[typeId]->Destroy( InIndex );
        }
    }

    m_guidToIndex.erase( record.GUID );

    const uint32_t nextGeneration = record.Generation + 1 == 0 ? 1 : record.Generation + 1;
    record = EntityRecord();
    record.Generation = nextGeneration;
    m_freeIndices.push_back( InIndex );
    --m_aliveCount;
}


void World::Simulate()
{
    if( IsLoading || m_iterationDepth > 0 )
    {
        return;
    }
    OPTICK_CATEGORY( "World::Simulate", Optick::Category::Scene )

    // Entities still being deserialized wait for a later sync point.
    std::vector<uint32_t> deferred;

    // Callbacks (OnEnable, OnEntityAdded...) may queue more work; keep going until it drains.
    for( int pass = 0; pass < 16; ++pass )
    {
        std::vector<uint32_t> queue;
        queue.swap( m_syncQueue );
        for( uint32_t index : queue )
        {
            EntityRecord& record = m_records[index];
            if( !record.Alive || !record.QueuedForSync )
            {
                continue;
            }
            if( record.IsLoading )
            {
                deferred.push_back( index );
                continue;
            }
            SyncEntity( index );
        }

        if( !m_destroyQueue.empty() )
        {
            std::vector<uint32_t> roots;
            roots.swap( m_destroyQueue );

            std::vector<EntityID> ordered;
            for( uint32_t index : roots )
            {
                if( m_records[index].Alive )
                {
                    CollectHierarchy( index, ordered );
                }
            }
            for( const EntityID& id : ordered )
            {
                DestroyEntityNow( id );
            }
        }

        if( m_syncQueue.empty() && m_destroyQueue.empty() )
        {
            break;
        }
    }

    m_syncQueue.insert( m_syncQueue.end(), deferred.begin(), deferred.end() );
}


void World::Start()
{
    for( BaseCore* core : m_sortedCores )
    {
        if( !core->IsRunning )
        {
            core->OnStart();
            core->IsRunning = true;
        }
    }
}


void World::Stop()
{
    for( BaseCore* core : m_sortedCores )
    {
        if( core->IsRunning )
        {
            core->OnStop();
            core->IsRunning = false;
        }
    }
}


void World::Destroy()
{
    // Every entity, children before parents.
    std::vector<EntityID> ordered;
    for( uint32_t index = 0; index < m_records.size(); ++index )
    {
        const EntityRecord& record = m_records[index];
        if( !record.Alive )
        {
            continue;
        }
        const Transform* transform = static_cast<const Transform*>( GetComponent( record.Facade.GetId(), Transform::GetStaticTypeId() ) );
        if( !transform || !transform->GetParentTransform() )
        {
            CollectHierarchy( index, ordered );
        }
    }
    for( const EntityID& id : ordered )
    {
        DestroyEntityNow( id );
    }
    for( uint32_t index = 0; index < m_records.size(); ++index )
    {
        if( m_records[index].Alive )
        {
            DestroyEntityNow( m_records[index].Facade.GetId() );
        }
    }
    m_syncQueue.clear();
    m_destroyQueue.clear();

    for( BaseCore* core : m_sortedCores )
    {
        core->Clear();
    }

    std::vector<BaseCore*> loaded = m_loadedCores;
    for( BaseCore* core : loaded )
    {
        core->OnStop();
        core->IsRunning = false;
        core->OnRemovedFromWorld();
        RemoveCore( core->GetTypeIdInternal() );
    }
}


void World::Unload()
{
    std::vector<uint32_t> doomed;
    for( uint32_t index = 0; index < m_records.size(); ++index )
    {
        if( m_records[index].Alive && m_records[index].DestroyOnLoad )
        {
            doomed.push_back( index );
        }
    }

    // Deepest first so parents are destroyed after their children.
    auto depthOf = [this]( uint32_t index ) {
        int depth = 0;
        const Transform* transform = static_cast<const Transform*>( GetComponent( m_records[index].Facade.GetId(), Transform::GetStaticTypeId() ) );
        while( transform && transform->GetParentTransform() && depth < 4096 )
        {
            transform = transform->GetParentTransform();
            ++depth;
        }
        return depth;
    };
    std::vector<std::pair<int, EntityID>> byDepth;
    byDepth.reserve( doomed.size() );
    for( uint32_t index : doomed )
    {
        byDepth.emplace_back( depthOf( index ), m_records[index].Facade.GetId() );
    }
    std::stable_sort( byDepth.begin(), byDepth.end(), []( const auto& a, const auto& b ) { return a.first > b.first; } );
    for( const auto& entry : byDepth )
    {
        DestroyEntityNow( entry.second );
    }
    m_destroyQueue.clear();

    std::vector<BaseCore*> loaded = m_loadedCores;
    for( BaseCore* core : loaded )
    {
        if( core->DestroyOnLoad )
        {
            core->OnStop();
            core->IsRunning = false;
            core->OnRemovedFromWorld();
            RemoveCore( core->GetTypeIdInternal() );
        }
    }
}


void World::UpdateLoadedCores( const UpdateContext& inUpdateContext )
{
    OPTICK_EVENT( "UpdateLoadedCores" );
    ++m_iterationDepth;
    for( BaseCore* core : m_loadedCores )
    {
        if( core->IsRunning )
        {
            OPTICK_EVENT_DYNAMIC( core->GetName().c_str() );
            ME_STAT_SCOPE( core->GetName().c_str() );
            core->Update( inUpdateContext );
        }
    }
    --m_iterationDepth;
}


void World::FixedUpdateLoadedCores( const UpdateContext& inUpdateContext )
{
    OPTICK_EVENT( "FixedUpdateLoadedCores" );
    ++m_iterationDepth;
    for( BaseCore* core : m_loadedCores )
    {
        if( core->IsRunning )
        {
            OPTICK_EVENT_DYNAMIC( core->GetName().c_str() );
            ME_STAT_SCOPE( core->GetName().c_str() );
            core->FixedUpdate( inUpdateContext );
        }
    }
    --m_iterationDepth;
}


void World::LateUpdateLoadedCores( const UpdateContext& inUpdateContext )
{
    OPTICK_EVENT( "LateUpdateLoadedCores" );
    ++m_iterationDepth;
    for( BaseCore* core : m_loadedCores )
    {
        if( core->IsRunning )
        {
            core->LateUpdate( inUpdateContext );
        }
    }
    --m_iterationDepth;
}

// ------------------------------------------------------------------------------------------------
// Cores
// ------------------------------------------------------------------------------------------------

BaseCore* World::AddCoreByName( const std::string& core )
{
    CoreRegistry& reg = GetCoreRegistry();
    CoreRegistry::iterator it = FindCoreFactory( core );

    if( it == reg.end() ) {
        CLog::GetInstance().Log( CLog::LogType::Error, "Factory not found for core " + core );
        return nullptr;
    }
    std::pair<BaseCore*, TypeId> createdCore = it->second( false );
    if( !HasCore( createdCore.second ) )
    {
        BaseCore* co = it->second( true ).first;
        AddCore( *co, createdCore.second, true );
        return co;
    }
    return GetCore( createdCore.second );
}


void World::AddCore( BaseCore& InCore, TypeId InCoreTypeId, bool OwnedByWorld )
{
    if( HasCore( InCoreTypeId ) )
    {
        if( OwnedByWorld && Cores[InCoreTypeId] != &InCore )
        {
            delete &InCore;
        }
        return;
    }

    Cores[InCoreTypeId] = &InCore;
    InCore.m_coreTypeId = InCoreTypeId;
    InCore.IsOwnedByWorld = OwnedByWorld;
    InCore.GameWorld = this;
    if( OwnedByWorld )
    {
        m_ownedCores.emplace_back( &InCore );
        m_loadedCores.push_back( &InCore );
    }
    SortCores();

    InCore.Init();
    InCore.OnAddedToWorld();

    // Existing entities may match the new core.
    for( EntityRecord& record : m_records )
    {
        if( record.Alive )
        {
            MarkEntityDirty( record.Facade.GetId() );
        }
    }
}


void World::RemoveCore( TypeId InType )
{
    auto it = Cores.find( InType );
    if( it == Cores.end() )
    {
        return;
    }
    BaseCore* core = it->second;
    Cores.erase( it );
    core->Clear();
    core->GameWorld = nullptr;
    m_sortedCores.erase( std::remove( m_sortedCores.begin(), m_sortedCores.end(), core ), m_sortedCores.end() );
    m_loadedCores.erase( std::remove( m_loadedCores.begin(), m_loadedCores.end(), core ), m_loadedCores.end() );
    m_ownedCores.erase( std::remove_if( m_ownedCores.begin(), m_ownedCores.end(), [core]( const std::unique_ptr<BaseCore>& owned ) { return owned.get() == core; } ), m_ownedCores.end() );
}


namespace
{
    // Priority then name, then the RunsAfter / RunsBefore constraints win: a stable topological
    // order where, among the cores free to run next, the first by priority and name goes first.
    void OrderCores( std::vector<BaseCore*>& InOutCores )
    {
        std::sort( InOutCores.begin(), InOutCores.end(), []( const BaseCore* InA, const BaseCore* InB ) {
            if( InA->GetPriority() != InB->GetPriority() )
            {
                return InA->GetPriority() < InB->GetPriority();
            }
            return InA->GetName() < InB->GetName();
        } );
        const size_t count = InOutCores.size();
        auto indexOf = [&InOutCores, count]( const std::string& InName ) {
            for( size_t i = 0; i < count; ++i )
            {
                if( InOutCores[i]->GetName() == InName )
                {
                    return i;
                }
            }
            return count;
        };
        std::vector<std::vector<size_t>> runsBefore( count );     // i -> the cores that wait for it
        std::vector<int> waitingOn( count, 0 );
        bool constrained = false;
        auto addEdge = [&]( size_t InFirst, size_t InThen ) {
            if( InFirst < count && InThen < count && InFirst != InThen )
            {
                runsBefore[InFirst].push_back( InThen );
                ++waitingOn[InThen];
                constrained = true;
            }
        };
        for( size_t i = 0; i < count; ++i )
        {
            for( const std::string& name : InOutCores[i]->GetRunsAfter() )
            {
                addEdge( indexOf( name ), i );
            }
            for( const std::string& name : InOutCores[i]->GetRunsBefore() )
            {
                addEdge( i, indexOf( name ) );
            }
        }
        if( !constrained )
        {
            return;
        }

        std::vector<BaseCore*> ordered;
        ordered.reserve( count );
        std::vector<bool> placed( count, false );
        while( ordered.size() < count )
        {
            size_t next = count;
            for( size_t i = 0; i < count && next == count; ++i )
            {
                if( !placed[i] && waitingOn[i] <= 0 )
                {
                    next = i;
                }
            }
            if( next == count )
            {
                for( size_t i = 0; i < count && next == count; ++i )
                {
                    if( !placed[i] )
                    {
                        next = i;
                    }
                }
                CLog::Log( CLog::LogType::Warning, "Core order: RunsAfter / RunsBefore constraints form a cycle through " + InOutCores[next]->GetName() + "; using priority order there" );
            }
            placed[next] = true;
            ordered.push_back( InOutCores[next] );
            for( size_t then : runsBefore[next] )
            {
                --waitingOn[then];
            }
        }
        InOutCores = std::move( ordered );
    }
}


void World::SortCores()
{
    m_sortedCores.clear();
    for( auto& core : Cores )
    {
        m_sortedCores.push_back( core.second );
    }
    OrderCores( m_sortedCores );
    OrderCores( m_loadedCores );
}


bool World::HasCore( TypeId InType )
{
    return Cores.find( InType ) != Cores.end();
}


BaseCore* World::GetCore( TypeId InType )
{
    auto it = Cores.find( InType );
    return it != Cores.end() ? it->second : nullptr;
}


std::vector<BaseCore*> World::GetAllCores()
{
    return m_sortedCores;
}


const World::CoreArray& World::GetAllCoresArray()
{
    return Cores;
}

// ------------------------------------------------------------------------------------------------
// Prefabs
// ------------------------------------------------------------------------------------------------

EntityHandle World::CreateFromPrefab( const std::string& FilePath, Transform* Parent )
{
    OPTICK_EVENT( "World::CreateFromPrefab" );
    return SceneSerializer::InstantiatePrefab( *this, FilePath, Parent );
}
