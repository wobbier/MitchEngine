#include "PCH.h"
#include "BindingContext.h"

#if USING( ME_SCRIPTING )

#include "ECS/ComponentDetail.h"
#include "ECS/ComponentPool.h"
#include "Engine/World.h"
#include "Cores/NavigationCore.h"
#include "Cores/Physics2DCore.h"
#include "Cores/PhysicsCore.h"
#include <unordered_map>

namespace ScriptBindings
{
    static WeakPtr<World> gDefaultWorld;
    // The innermost WorldScope on this thread.
    static thread_local WeakPtr<World> tScopedWorld;
    static thread_local bool tHasScope = false;
    static std::unordered_map<std::string, std::function<bool( EntityHandle )>> gHasComponent;
    static std::unordered_map<std::string, std::function<void( EntityHandle )>> gAddComponent;

    void SetWorld( WeakPtr<World> inWorld )
    {
        gDefaultWorld = inWorld;
    }


    WeakPtr<World> GetWorld()
    {
        return tHasScope ? tScopedWorld : gDefaultWorld;
    }


    WorldScope::WorldScope( WeakPtr<World> InWorld )
        : m_previous( tScopedWorld )
        , m_hadPrevious( tHasScope )
    {
        tScopedWorld = std::move( InWorld );
        tHasScope = true;
    }


    WorldScope::~WorldScope()
    {
        tScopedWorld = std::move( m_previous );
        tHasScope = m_hadPrevious;
    }


    PhysicsCore* Physics()
    {
        SharedPtr<World> world = GetWorld().lock();
        return world ? world->GetCore<PhysicsCore>() : nullptr;
    }


    Physics2DCore* Physics2D()
    {
        SharedPtr<World> world = GetWorld().lock();
        return world ? world->GetCore<Physics2DCore>() : nullptr;
    }


    NavigationCore* Navigation()
    {
        SharedPtr<World> world = GetWorld().lock();
        return world ? world->GetCore<NavigationCore>() : nullptr;
    }


    EntityHandle MakeHandle( EntityID inId )
    {
        return EntityHandle( inId, GetWorld() );
    }


    void RegisterComponentOps( const std::string& inName, std::function<bool( EntityHandle )> inHas, std::function<void( EntityHandle )> inAdd )
    {
        gHasComponent[inName] = std::move( inHas );
        gAddComponent[inName] = std::move( inAdd );
    }


    // Whether the entity has a component of that type (Entity's by-type check is World's).
    bool HasComponentOfType( const Entity& InEntity, TypeId InType )
    {
        SharedPtr<World> world = GetWorld().lock();
        IComponentPool* pool = world ? world->GetComponentPool( InType ) : nullptr;
        return pool != nullptr && pool->Get( InEntity.GetId().Index ) != nullptr;
    }


    // Names registered here win (their own add/has logic); every other component the engine
    // registers (ME_REGISTER_COMPONENT) works through the component registry.
    bool HasComponentNamed( EntityHandle inHandle, const char* inName )
    {
        auto it = gHasComponent.find( inName );
        if( it != gHasComponent.end() )
        {
            return it->second( inHandle );
        }
        auto info = FindComponentInfo( inName );
        Entity* entity = inHandle ? inHandle.Get() : nullptr;
        return entity && info != GetComponentRegistry().end() && HasComponentOfType( *entity, info->second.GetTypeFunc() );
    }


    bool AddComponentNamed( EntityHandle inHandle, const char* inName )
    {
        auto it = gAddComponent.find( inName );
        if( it != gAddComponent.end() )
        {
            it->second( inHandle );
            return true;
        }
        auto info = FindComponentInfo( inName );
        Entity* entity = inHandle ? inHandle.Get() : nullptr;
        if( !entity || info == GetComponentRegistry().end() )
        {
            return false;
        }
        if( !HasComponentOfType( *entity, info->second.GetTypeFunc() ) )
        {
            info->second.CreateFunc( *entity );
        }
        return true;
    }
}

#endif
