#include "PCH.h"
#include "Camera.bindings.h"

#if USING( ME_SCRIPTING )

#include <Components/Camera.h>
#include "CLog.h"
#include "Components/Graphics/Mesh.h"
#include "Materials/PortalMaterial.h"
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


static void Eng_Camera_SetClipPlane( EntityID inId, const Vector3* inPoint, const Vector3* inNormal )
{
    EntityHandle handle = MakeHandle( inId );
    if( Camera* camera = handle ? handle->TryGetComponent<Camera>() : nullptr )
    {
        camera->SetClipPlane( *inPoint, *inNormal );
    }
}


static void Eng_Camera_ClearClipPlane( EntityID inId )
{
    EntityHandle handle = MakeHandle( inId );
    if( Camera* camera = handle ? handle->TryGetComponent<Camera>() : nullptr )
    {
        camera->ClearClipPlane();
    }
}


// Gives the mesh a PortalMaterial showing the camera's view (keeping a PortalMaterial it already
// has, with its tint) and sizes the camera to match the main camera.
static bool Eng_Mesh_ShowCameraView( EntityID inId, EntityID inCamera )
{
    EntityHandle handle = MakeHandle( inId );
    EntityHandle cameraHandle = MakeHandle( inCamera );
    Mesh* mesh = handle ? handle->TryGetComponent<Mesh>() : nullptr;
    Camera* camera = cameraHandle ? cameraHandle->TryGetComponent<Camera>() : nullptr;
    if( !mesh || !camera )
    {
        YIKES( "Mesh.ShowCameraView needs a Mesh on the entity and a Camera on the camera entity" );
        return false;
    }
    if( !mesh->MeshMaterial || mesh->MeshMaterial->GetTypeName() != "PortalMaterial" )
    {
        mesh->MeshMaterial = MakeShared<PortalMaterial>();
    }
    mesh->MeshMaterial->ScreenViewCamera = camera->GetCameraId();
    mesh->CastShadows = false;
    camera->MatchMainCamera = true;
    return true;
}


void Register_CameraBindings( ScriptEngineAPI& inAPI )
{
    ScriptBindings::RegisterComponent<Camera>( "Camera" );
    ScriptBindings::RegisterComponent<Mesh>( "Mesh" );

    inAPI.Camera_GetClearColor = Eng_Camera_GetClearColor;
    inAPI.Camera_SetClearColor = Eng_Camera_SetClearColor;
    inAPI.Camera_SetClipPlane = Eng_Camera_SetClipPlane;
    inAPI.Camera_ClearClipPlane = Eng_Camera_ClearClipPlane;
    inAPI.Mesh_ShowCameraView = Eng_Mesh_ShowCameraView;
}

#endif
