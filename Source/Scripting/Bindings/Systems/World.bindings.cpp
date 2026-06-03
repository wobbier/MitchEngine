#include "PCH.h"
#include "World.bindings.h"

#if USING( ME_SCRIPTING )

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Cores/SceneCore.h"
#include "Components/Transform.h"
#include "ECS/Entity.h"
#include "Scripting/Bindings/BindingContext.h"

static void Eng_World_CreateEntity( const uint8_t* inName, EntityID* outId )
{
    *outId = EntityID{};
    auto world = ScriptBindings::GetWorld().lock();
    if( !world )
    {
        return;
    }

    EntityHandle newEntity = world->CreateEntity();
    Transform& transform = newEntity->AddComponent<Transform>();
    transform.SetName( reinterpret_cast<const char*>( inName ) );
    world->Simulate();
    *outId = newEntity.GetID();
}


static void Eng_World_FindByName( const uint8_t* inName, EntityID* outId )
{
    *outId = EntityID{};
    // #TODO: FIX - this shit is busted. only scans the scene root's direct children, so anything
    // nested deeper never gets found. needs a proper recursive walk (or a name->entity lookup).
    Transform* root = GetEngine().SceneNodes->GetRootTransform();
    if( !root )
    {
        return;
    }

    const std::string wanted = reinterpret_cast<const char*>( inName );
    for( auto& child : root->GetChildren() )
    {
        if( child->GetName() == wanted )
        {
            *outId = child->Parent.GetID();
            return;
        }
    }
}


void Register_WorldBindings( ScriptEngineAPI& inAPI )
{
    inAPI.World_CreateEntity = Eng_World_CreateEntity;
    inAPI.World_FindByName = Eng_World_FindByName;
}

#endif
