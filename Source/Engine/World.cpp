#include "PCH.h"
#include "Engine/World.h"
#include "Components/Transform.h"
#include <unordered_map>
#include "Pointers.h"
#include "CLog.h"
#include "ECS/CoreDetail.h"
#include "File.h"
#include "Resources/JsonResource.h"
#include "optick.h"
#include "Core/Assert.h"

#define DEFAULT_ENTITY_POOL_SIZE 50

World::World()
    : World( DEFAULT_ENTITY_POOL_SIZE )
{
}

World::World( std::size_t InEntityPoolSize ) :
    EntIdPool( InEntityPoolSize ),
    EntityAttributes( InEntityPoolSize )
{
}

World::~World()
{
}

BaseCore* World::AddCoreByName( const std::string& core )
{
    CoreRegistry& reg = GetCoreRegistry();
    CoreRegistry::iterator it = reg.find( core );

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

EntityHandle World::CreateEntity()
{
    CheckForResize( 1 );
    EntityID id = EntIdPool.Create();
    EntityCache.Alive[id] = Entity( *this, id );
    return EntityHandle( id, GetSharedPtr() );
}

void World::Simulate()
{
    if( IsLoading )
    {
        return;
    }
    OPTICK_CATEGORY( "World::Simulate", Optick::Category::Scene )
        for( auto& InEntity : EntityCache.Activated )
        {
            auto& Attr = EntityAttributes.Attributes[InEntity.GetId().Index];
            Attr.IsActive = true;

            for( auto& InCore : Cores )
            {
                if( !InCore.second->IsRunning )
                {
                    //continue;
                }
                auto CoreIndex = InCore.first;

                if( InCore.second->GetComponentFilter().PassFilter( EntityAttributes.Storage.GetComponentTypes( InEntity ) ) )
                {
                    if( Attr.Cores.size() <= CoreIndex || !Attr.Cores[CoreIndex] )
                    {
                        InCore.second->Add( InEntity );

                        Attr.Cores[CoreIndex] = true;
                    }
                }
                else if( Attr.Cores.size() > CoreIndex && Attr.Cores[CoreIndex] )
                {
                    InCore.second->Remove( InEntity );
                    Attr.Cores[CoreIndex] = false;
                }
            }
        }
    for( auto& InEntity : EntityCache.Deactivated )
    {
        auto& Attr = EntityAttributes.Attributes[InEntity.GetId().Index];
        if( Attr.IsActive )
        {
            Attr.IsActive = false;

            for( auto& InCore : Cores )
            {
                auto CoreIndex = InCore.first;
                if( Attr.Cores.size() <= CoreIndex ) continue;
                if( Attr.Cores[CoreIndex] )
                {
                    InCore.second->Remove( InEntity );
                    Attr.Cores[CoreIndex] = false;
                }
            }
        }
    }

    for( auto& InEntity : EntityCache.Killed )
    {
        DestroyEntity( InEntity, true );
    }

    EntityCache.ClearTemp();
}

void World::Start()
{
    for( auto& core : Cores )
    {
        if( !core.second->IsRunning )
        {
            core.second->OnStart();
            core.second->IsRunning = true;
        }
    }
}

void World::Stop()
{
    for( auto& core : Cores )
    {
        if( core.second->IsRunning )
        {
            core.second->OnStop();
            core.second->IsRunning = false;
        }
    }
}

void World::Destroy()
{
    //sus entities are maybe alive?

    for( auto& it : EntityCache.Alive )
    {
        DestroyEntity( it.second, false );
    }
    EntityCache.Alive.clear();

    for( auto& core : Cores )
    {
        core.second->Clear();
    }
    for( auto& core : m_loadedCores )
    {
        core.second->OnStop();
        core.second->OnRemovedFromWorld();
        Cores.erase( core.first );
    }
    m_loadedCores.clear();
    EntityCache.ClearTemp();
}

void World::Unload()
{
    for( auto iter = EntityCache.Alive.begin(); iter != EntityCache.Alive.end(); )
    {
        if( iter->second.DestroyOnLoad )
        {
            DestroyEntity( iter->second, false );
            iter = EntityCache.Alive.erase( iter );
        }
        else
        {
            ++iter;
        }
    }

    for( auto it = m_loadedCores.begin(); it != m_loadedCores.end(); /* no increment here */ )
    {
        if( it->second->DestroyOnLoad )
        {
            it->second->OnStop();

            it->second->OnRemovedFromWorld();

            Cores.erase( it->first );
            it = m_loadedCores.erase( it );
        }
        else
        {
            ++it;
        }
    }

    // Do I need this?
    EntityCache.ClearTemp();
}

void World::UpdateLoadedCores( const UpdateContext& inUpdateContext )
{
    OPTICK_EVENT( "UpdateLoadedCores" );
    for( auto core : m_loadedCores )
    {
        if( core.second && core.second->IsRunning )
        {
            OPTICK_EVENT( core.second->GetName().c_str() );
            core.second->Update( inUpdateContext );
        }
    }
}

void World::LateUpdateLoadedCores( const UpdateContext& inUpdateContext )
{
    OPTICK_EVENT( "UpdateLoadedCores" );
    for( auto core : m_loadedCores )
    {
        if( core.second && core.second->IsRunning )
        {
            core.second->LateUpdate( inUpdateContext );
        }
    }
}

void World::DestroyEntity( Entity& InEntity, bool RemoveFromWorld )
{
    auto& Attr = EntityAttributes.Attributes[InEntity.GetId().Index];
    Attr.IsActive = false;
    {
        for( auto& InCore : Cores )
        {
            auto CoreIndex = InCore.first;

            if( InCore.second->GetComponentFilter().PassFilter( EntityAttributes.Storage.GetComponentTypes( InEntity ) ) )
            {
                if( Attr.Cores.size() >= CoreIndex )
                {
                    InCore.second->Remove( InEntity );
                    InCore.second->OnEntityDestroyed( InEntity );
                }
            }
        }
    }
    EntityAttributes.Storage.RemoveAllComponents( InEntity );
    Attr.Cores.reset();

    EntIdPool.Remove( InEntity.GetId() );

    if( RemoveFromWorld )
    {
        auto it = EntityCache.Alive.find( InEntity.GetId() );
        if( it != EntityCache.Alive.end() )
        {
            EntityCache.Alive.erase( it );
        }
    }
}

EntityHandle World::CreateFromPrefab( std::string& FilePath, Transform* Parent )
{
    OPTICK_EVENT( "World::CreateFromPrefab" );
    //File PrefabSource = File(Path(FilePath));
    SharedPtr<JsonResource> prefabJson = ResourceCache::GetInstance().Get<JsonResource>( Path( FilePath ) );
    //nlohmann::json Prefab = nlohmann::json::parse(PrefabSource.Read());
    return LoadPrefab( prefabJson->GetJson(), Parent, Parent );
}

EntityHandle World::LoadPrefab( const json& obj, Transform* parent, Transform* root )
{
    EntityHandle ent;
    World* GameWorld = this;
    ME_ASSERT_MSG( GameWorld, "Trying to load a Prefab into a null world." );
    if( parent && parent != root )
    {
        auto t = parent->GetChildByName( obj["Name"] );
        if( t )
        {
            ent = t->Parent;
        }
    }
    if( !ent )
    {
        ent = GameWorld->CreateEntity();
    }
    ent->IsLoading = true;
    Transform* transComp = nullptr;
    for( const json& comp : obj["Components"] )
    {
        if( comp.is_null() )
        {
            continue;
        }
        BaseComponent* addedComp = ent->AddComponentByName( comp["Type"] );
        if( comp["Type"] == "Transform" )
        {
            transComp = static_cast<Transform*>( addedComp );
            if( parent )
            {
                transComp->SetParent( *parent );
            }
            transComp->SetName( obj["Name"] );
        }
        if( addedComp )
        {
            addedComp->Deserialize( comp );
            addedComp->Init();
        }
    }
    ent->SetActive( true );
    ent->IsLoading = false;

    if( obj.contains( "Children" ) )
    {
        for( const json& child : obj["Children"] )
        {
            LoadPrefab( child, transComp, root );
        }
    }
    return ent;
}

SharedPtr<World> World::GetSharedPtr()
{
    return shared_from_this();
}

void World::AddCore( BaseCore& InCore, TypeId InCoreTypeId, bool HandleUpdate )
{
    if( !Cores[InCoreTypeId] )
    {
        Cores[InCoreTypeId].reset( &InCore );
    }
    InCore.GameWorld = this;
    InCore.Init();
    if( HandleUpdate )
    {
        m_loadedCores[InCoreTypeId] = Cores[InCoreTypeId].get();
    }
    InCore.OnAddedToWorld();
    // 	for (auto ent : EntityCache.Alive)
    // 	{
    // 		ActivateEntity(*ent.get(), true);
    // 	}
}

bool World::HasCore( TypeId InType )
{
    return Cores.find( InType ) != Cores.end();
}

BaseCore* World::GetCore( TypeId InType )
{
    return Cores[InType].get();
}

std::vector<BaseCore*> World::GetAllCores()
{
    OPTICK_CATEGORY( "GetAllCores", Optick::Category::Scene );

    std::vector<BaseCore*> cores;
    for( auto& core : Cores )
    {
        cores.push_back( core.second.get() );
    }
    return cores;
}

const World::CoreArray& World::GetAllCoresArray()
{
    return Cores;
}

void World::CheckForResize( std::size_t InNumEntitiesToBeAllocated )
{
    auto NewSize = GetEntityCount() + InNumEntitiesToBeAllocated;

    if( NewSize > EntIdPool.GetSize() )
    {
        Resize( NewSize );
    }
}

void World::Resize( std::size_t InAmount )
{
    EntIdPool.Resize( InAmount );
    EntityAttributes.Resize( InAmount );
}

std::size_t World::GetEntityCount() const
{
    return EntityCache.Alive.size();
}

EntityHandle World::GetEntity( const EntityID& InEntity )
{
    auto it = EntityCache.Alive.find( InEntity );
    if( it != EntityCache.Alive.end() )
    {
        return EntityHandle( InEntity, GetSharedPtr() );
    }

    return {};
}

Entity* World::GetEntityRaw( const EntityID& InEntity )
{
    auto it = EntityCache.Alive.find( InEntity );
    if( it != EntityCache.Alive.end() )
    {
        return &EntityCache.Alive[InEntity];
    }

    return nullptr;
}

const bool World::EntityExists( const EntityID& InEntity ) const
{
    auto it = EntityCache.Alive.find( InEntity );
    return ( it != EntityCache.Alive.end() );
}

void World::ActivateEntity( Entity& InEntity, const bool InActive )
{
    if( InActive )
    {
        EntityCache.Activated.push_back( InEntity );
    }
    else
    {
        EntityCache.Deactivated.push_back( InEntity );
    }
}

void World::MarkEntityForDelete( Entity& EntityToDestroy )
{
    //EntityCache.Deactivated.push_back(EntityToDestroy);
    EntityCache.Killed.push_back( EntityToDestroy );
}
