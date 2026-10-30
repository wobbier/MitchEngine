#include "PCH.h"
#include "Camera.bindings.h"

#if USING( ME_SCRIPTING )

#include <Components/Camera.h>
#include "CLog.h"
#include "Components/Graphics/Mesh.h"
#include "Components/Transform.h"
#include "Materials/PortalMaterial.h"
#include "ECS/Entity.h"
#include "Scripting/Bindings/BindingContext.h"

using ScriptBindings::MakeHandle;

static void Eng_Camera_GetClearColor( EntityID inId, Vector3* outColor ) noexcept
{
    EntityHandle handle = MakeHandle( inId );
    if( handle )
    {
        *outColor = handle->GetComponent<Camera>().ClearColor;
    }
}


static void Eng_Camera_SetClearColor( EntityID inId, const Vector3* inColor ) noexcept
{
    EntityHandle handle = MakeHandle( inId );
    if( handle )
    {
        handle->GetComponent<Camera>().ClearColor = *inColor;
    }
}


static void Eng_Camera_SetClipPlane( EntityID inId, const Vector3* inPoint, const Vector3* inNormal ) noexcept
{
    EntityHandle handle = MakeHandle( inId );
    if( Camera* camera = handle ? handle->TryGetComponent<Camera>() : nullptr )
    {
        camera->SetClipPlane( *inPoint, *inNormal );
    }
}


static void Eng_Camera_ClearClipPlane( EntityID inId ) noexcept
{
    EntityHandle handle = MakeHandle( inId );
    if( Camera* camera = handle ? handle->TryGetComponent<Camera>() : nullptr )
    {
        camera->ClearClipPlane();
    }
}


// Gives the mesh a PortalMaterial showing the camera's view (keeping a PortalMaterial it already
// has, with its tint) and sizes the camera to match the main camera.
static bool Eng_Mesh_ShowCameraView( EntityID inId, EntityID inCamera ) noexcept
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


// Called each frame for visible pieces of a travelling rigidbody. The proxy shares geometry
// and material, but owns its transform/clip plane and has no collider or gameplay components.
static void Eng_Mesh_SetPortalSlice( EntityID inSource, EntityID inImage, EntityID inEntrance, EntityID inExit ) noexcept
{
    EntityHandle source = MakeHandle( inSource ), image = MakeHandle( inImage );
    EntityHandle entrance = MakeHandle( inEntrance ), exit = MakeHandle( inExit );
    Mesh* mesh = source ? source->TryGetComponent<Mesh>() : nullptr;
    if( !mesh ) return;
    mesh->PortalClipPlane = glm::vec4( 0.f );
    if( !entrance || !exit || !image ) return;
    Mesh* proxy = image->TryGetComponent<Mesh>();
    if( !proxy ) return;
    auto& a = entrance->GetComponent<Transform>();
    auto& b = exit->GetComponent<Transform>();
    const glm::vec3 normal = a.Front().InternalVector;
    const glm::vec3 exitNormal = b.Front().InternalVector;
    mesh->PortalClipPlane = glm::vec4( normal, -glm::dot( normal, a.GetWorldPosition().InternalVector ) );
    proxy->PortalClipPlane = glm::vec4( exitNormal, -glm::dot( exitNormal, b.GetWorldPosition().InternalVector ) );
    glm::mat4 turn( 1.f ); turn[0][0] = -1.f; turn[2][2] = -1.f;
    proxy->PortalImageWorld = Matrix4( b.GetLocalToWorldMatrix().GetInternalMatrix() * turn
        * glm::inverse( a.GetLocalToWorldMatrix().GetInternalMatrix() )
        * source->GetComponent<Transform>().GetLocalToWorldMatrix().GetInternalMatrix() );
    proxy->PortalImage = true;
    if( proxy->MeshReferece != mesh->MeshReferece ) proxy->BindModelMesh( mesh->MeshReferece );
    proxy->MeshMaterial = mesh->MeshMaterial;
    proxy->CastShadows = mesh->CastShadows;
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
    inAPI.Mesh_SetPortalSlice = Eng_Mesh_SetPortalSlice;
}

#endif
