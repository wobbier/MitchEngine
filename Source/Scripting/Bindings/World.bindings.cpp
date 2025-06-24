#include "PCH.h"

#include <mono/metadata/object.h>
#include <Components/Transform.h>
#include <Scripting/ScriptEngine.h>
#include "Engine/Engine.h"
#include "Cores/SceneCore.h"


static const Transform* World_GetTransformByName_InternalRecursive( Transform* inParent, const std::string& inString )
{
    for( auto& transform : inParent->GetChildren() )
    {
        if( transform->GetName() == inString )
        {
            return transform.get();
        }
    }
    return nullptr;
}


static MonoObject* World_GetTransformByName( MonoString* inString )
{
    char* str = mono_string_to_utf8( inString );
    const Transform* found = World_GetTransformByName_InternalRecursive( ScriptEngine::sScriptData.enginePtr->SceneNodes->GetRootTransform(), str );

    mono_free( str );
    if( !found )
    {
        return nullptr;
    }

    return ScriptEngine::ReturnEntityID( found->Parent );
}


static MonoObject* World_CreateEntity( MonoString* inString )
{
    char* str = mono_string_to_utf8( inString );

    auto world = ScriptEngine::sScriptData.worldPtr.lock();
    auto newEntity = world->CreateEntity();
    Transform* found = &newEntity->AddComponent<Transform>();
    found->SetName( str );

    mono_free( str );

    return ScriptEngine::ReturnEntityID( newEntity );
}


void Register_WorldBindings()
{
    mono_add_internal_call( "World::World_GetTransformByName", (void*)World_GetTransformByName );
    mono_add_internal_call( "World::World_CreateEntity", (void*)World_CreateEntity );
}
