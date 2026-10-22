#include "PCH.h"
#include "Camera.bindings.h"

#if USING( ME_SCRIPTING )

#include <Components/Camera.h>
#include "ECS/Entity.h"
#include "Scripting/Bindings/BindingContext.h"

using ScriptBindings::MakeHandle;

static void Eng_Camera_GetClearColor( EntityID inId, Vector3* outColor )
{
    EntityHandle handle = MakeHandle( inId );
    if( handle )
    {
        *outColor = handle->GetComponent<Camera>().ClearColor;
    }
}


static void Eng_Camera_SetClearColor( EntityID inId, const Vector3* inColor )
{
    EntityHandle handle = MakeHandle( inId );
    if( handle )
    {
        handle->GetComponent<Camera>().ClearColor = *inColor;
    }
}


void Register_CameraBindings( ScriptEngineAPI& inAPI )
{
    ScriptBindings::RegisterComponent<Camera>( "Camera" );

    inAPI.Camera_GetClearColor = Eng_Camera_GetClearColor;
    inAPI.Camera_SetClearColor = Eng_Camera_SetClearColor;
}

#endif
