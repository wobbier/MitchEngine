#include "PCH.h"
#include "Entity.bindings.h"

#if USING( ME_SCRIPTING )

#include "ECS/Entity.h"
#include "Engine/World.h"
#include "BindingContext.h"

using ScriptBindings::MakeHandle;

static bool Eng_Entity_IsAlive( EntityID inId )
{
    return static_cast<bool>( MakeHandle( inId ) );
}


static bool Eng_Entity_HasComponent( EntityID inId, const uint8_t* inComponent )
{
    EntityHandle handle = MakeHandle( inId );
    if( !handle )
    {
        return false;
    }

    return ScriptBindings::HasComponentNamed( handle, reinterpret_cast<const char*>( inComponent ) );
}


static void Eng_Entity_AddComponent( EntityID inId, const uint8_t* inComponent )
{
    EntityHandle handle = MakeHandle( inId );
    if( !handle )
    {
        return;
    }

    if( !ScriptBindings::AddComponentNamed( handle, reinterpret_cast<const char*>( inComponent ) ) )
    {
        BRUH_NEW( "dotnet: AddComponent for unknown type '{}'", reinterpret_cast<const char*>( inComponent ) );
        return;
    }

    // create the entity created NOW, I should do the OW thing where entities are created at the end of the frame.
    if( auto world = ScriptBindings::GetWorld().lock() )
    {
        world->Simulate();
    }
}


void Register_EntityBindings( ScriptEngineAPI& inAPI )
{
    inAPI.Entity_IsAlive = Eng_Entity_IsAlive;
    inAPI.Entity_HasComponent = Eng_Entity_HasComponent;
    inAPI.Entity_AddComponent = Eng_Entity_AddComponent;
}

#endif
