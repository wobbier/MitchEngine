#include "PCH.h"
#include "World.bindings.h"

#if USING( ME_SCRIPTING )

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/LoadingScreen.h"
#include "Cores/SceneCore.h"
#include "Components/Transform.h"
#include "ECS/Entity.h"
#include "Scripting/Bindings/BindingContext.h"
#include <cstdlib>

static void Eng_World_CreateEntity( const uint8_t* inName, EntityID* outId ) noexcept
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


static void Eng_World_FindByName( const uint8_t* inName, EntityID* outId ) noexcept
{
    // Any live entity with that name, at any depth.
    *outId = EntityID{};
    auto world = ScriptBindings::GetWorld().lock();
    if( !world )
    {
        return;
    }
    if( EntityHandle found = world->FindEntityByName( reinterpret_cast<const char*>( inName ) ) )
    {
        *outId = found.GetID();
    }
}


static bool Eng_World_FindByGuid( const uint8_t* inGuid, EntityID* outId ) noexcept
{
    // The 16-hex scene GUID, as EntityRef fields and .lvl files store it.
    *outId = EntityID{};
    auto world = ScriptBindings::GetWorld().lock();
    if( !world )
    {
        return false;
    }
    const uint64_t guid = std::strtoull( reinterpret_cast<const char*>( inGuid ), nullptr, 16 );
    if( guid == 0 )
    {
        return false;
    }
    if( EntityHandle found = world->FindEntityByGUID( guid ) )
    {
        *outId = found.GetID();
        return true;
    }
    return false;
}


static bool Eng_World_LoadSceneAsync( const uint8_t* inScene, const uint8_t* inDocument ) noexcept
{
    return LoadingScreen::Get().LoadSceneAsync( reinterpret_cast<const char*>( inScene ), inDocument ? reinterpret_cast<const char*>( inDocument ) : "" );
}


static float Eng_World_GetLoadProgress() noexcept
{
    return LoadingScreen::Get().GetProgress();
}


static bool Eng_World_IsLoadingScene() noexcept
{
    return LoadingScreen::Get().IsLoading();
}


void Register_WorldBindings( ScriptEngineAPI& inAPI )
{
    inAPI.World_LoadSceneAsync = Eng_World_LoadSceneAsync;
    inAPI.World_GetLoadProgress = Eng_World_GetLoadProgress;
    inAPI.World_IsLoadingScene = Eng_World_IsLoadingScene;
    inAPI.World_CreateEntity = Eng_World_CreateEntity;
    inAPI.World_FindByName = Eng_World_FindByName;
    inAPI.World_FindByGuid = Eng_World_FindByGuid;
}

#endif
