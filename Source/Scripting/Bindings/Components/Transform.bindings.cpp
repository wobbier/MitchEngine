#include "PCH.h"
#include "Transform.bindings.h"

#if USING( ME_SCRIPTING )

#include <Components/Transform.h>
#include "ECS/Entity.h"
#include "Scripting/Bindings/BindingContext.h"

using ScriptBindings::MakeHandle;

static void Eng_Transform_GetTranslation( EntityID inId, Vector3* outTranslation )
{
    EntityHandle handle = MakeHandle( inId );
    if( handle )
    {
        *outTranslation = handle->GetComponent<Transform>().GetPosition();
    }
}


static void Eng_Transform_SetTranslation( EntityID inId, const Vector3* inTranslation )
{
    EntityHandle handle = MakeHandle( inId );
    if( handle )
    {
        handle->GetComponent<Transform>().SetPosition( *inTranslation );
    }
}


static void Eng_Transform_GetScale( EntityID inId, Vector3* outScale )
{
    EntityHandle handle = MakeHandle( inId );
    if( handle )
    {
        *outScale = handle->GetComponent<Transform>().GetScale();
    }
}


static void Eng_Transform_SetScale( EntityID inId, const Vector3* inScale )
{
    EntityHandle handle = MakeHandle( inId );
    if( handle )
    {
        handle->GetComponent<Transform>().SetScale( *inScale );
    }
}


static void Eng_Transform_GetRotation( EntityID inId, Vector3* outEuler )
{
    EntityHandle handle = MakeHandle( inId );
    if( handle )
    {
        *outEuler = handle->GetComponent<Transform>().GetRotationEuler();
    }
}


static void Eng_Transform_SetRotation( EntityID inId, const Vector3* inEuler )
{
    EntityHandle handle = MakeHandle( inId );
    if( handle )
    {
        handle->GetComponent<Transform>().SetRotation( *inEuler );
    }
}


void Register_TransformBindings( ScriptEngineAPI& inAPI )
{
    ScriptBindings::RegisterComponent<Transform>( "Transform" );

    inAPI.Transform_GetTranslation = Eng_Transform_GetTranslation;
    inAPI.Transform_SetTranslation = Eng_Transform_SetTranslation;
    inAPI.Transform_GetScale       = Eng_Transform_GetScale;
    inAPI.Transform_SetScale       = Eng_Transform_SetScale;
    inAPI.Transform_GetRotation    = Eng_Transform_GetRotation;
    inAPI.Transform_SetRotation    = Eng_Transform_SetRotation;
}

#endif
