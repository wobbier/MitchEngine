#include "PCH.h"
#include "CameraCore.h"
#include "Components/Camera.h"
#include "Engine/Engine.h"
#include "Window/IWindow.h"
#include "Math/Frustrum.h"
#include "Camera/CameraData.h"
#include "Renderer.h"
#include "Mathf.h"
#include <Core/Memory.h>

CameraCore::CameraCore()
    : Base( ComponentFilter().Requires<Camera>().Requires<Transform>() )
{
    SetIsSerializable( false );
    DefaultCamera = ME_NEW Camera();
}

CameraCore::~CameraCore()
{
    ME_DELETE( DefaultCamera );
}

void CameraCore::Init()
{
    DefaultCamera->SetCurrent();
}

void CameraCore::Update( const UpdateContext& inUpdateContext )
{
}

void CameraCore::LateUpdate( const UpdateContext& inUpdateContext )
{
    OPTICK_CATEGORY( "CameraCore::Update", Optick::Category::Camera );
    auto Cameras = GetEntities();
    BGFXRenderer* renderer = inUpdateContext.GetSystem<BGFXRenderer>();
    Vector2 windowSize = GetEngine().GetWindow()->GetSize();
    CommandCache<Moonlight::CameraData>& cameraCache = renderer->GetCameraCache();

    for( auto& InEntity : Cameras )
    {
        // Get our components that our Core required
        Camera& CameraComponent = InEntity.GetComponent<Camera>();
        Transform& TransformComponent = InEntity.GetComponent<Transform>();

        if( CameraComponent.IsMain() )
        {
            CameraComponent.OutputSize = windowSize;
        }

        Moonlight::CameraData* CamData = cameraCache.Get( CameraComponent.m_id );
        if( CamData )
        {
            CamData->Position = TransformComponent.GetWorldPosition();
            CamData->Front = TransformComponent.Front();
            CamData->Up = TransformComponent.Up();
            CamData->OutputSize = { Mathf::Abs( CameraComponent.OutputSize.x ), Mathf::Abs( CameraComponent.OutputSize.y ) };

            CamData->FOV = CameraComponent.GetFOV();
            CamData->Near = CameraComponent.Near;
            CamData->Far = CameraComponent.Far;
            CamData->Skybox = CameraComponent.Skybox;
            CamData->ClearColor = CameraComponent.ClearColor;
            CamData->ClearType = CameraComponent.ClearType;
            CamData->Projection = CameraComponent.Projection;
            CamData->OrthographicSize = CameraComponent.OrthographicSize;
            CamData->IsMain = CameraComponent.IsMain();
            //CamData.CameraFrustum = CameraComponent.CameraFrustum;
            CamData->UITexture = BGFX_INVALID_HANDLE;

            Vector3 eye = { CamData->Position.x, CamData->Position.y, CamData->Position.z };
            Vector3 at = { CamData->Position.x + CamData->Front.x, CamData->Position.y + CamData->Front.y, CamData->Position.z + CamData->Front.z };
            Vector3 up = { CamData->Up.x, CamData->Up.y, CamData->Up.z };

            Frustum& camFrustum = CameraComponent.CameraFrustum;
            //bx::mtxProj( &CameraComponent.WorldToCamera.GetInternalMatrix()[0][0], CameraComponent.GetFOV(), float( CameraComponent.OutputSize.x ) / float( CameraComponent.OutputSize.y ), std::max( CameraComponent.Near, 1.f ), CameraComponent.Far, bgfx::getCaps()->homogeneousDepth );
            CamData->IsOblique = CameraComponent.isOblique;
            CamData->ObliqueData = CameraComponent.ObliqueMatData;
            CamData->View = glm::lookAtLH( eye.InternalVector, at.InternalVector, up.InternalVector );


            //Matrix4 testMatrix;
            //bx::mtxProj( &testMatrix.GetInternalMatrix()[0][0], CamData->FOV, float( CamData->OutputSize.x ) / float( CamData->OutputSize.y ), std::max( CamData->Near, 0.01f ), CamData->Far, bgfx::getCaps()->homogeneousDepth );
            CamData->ProjectionMatrix = CameraComponent.GetProjectionMatrix();
            camFrustum.Update( CamData->ProjectionMatrix, CamData->View, CamData->FOV, CamData->OutputSize, CamData->Near, CamData->Far );
            CamData->ViewFrustum = camFrustum;


            //Matrix4 testMatrix;
            //camFrustum.Update( testMatrix, CamData->View, CameraComponent.GetFOV(), CameraComponent.OutputSize, CameraComponent.Near, CameraComponent.Far );
            //CamData->ViewFrustum = camFrustum;
            //CamData->ProjectionMatrix = testMatrix;


            CameraComponent.WorldToCamera = CamData->View;// CamData->ProjectionMatrix.GetInternalMatrix()* CamData->View.GetInternalMatrix();
            cameraCache.Update( CameraComponent.m_id, *CamData );
        }
    }
}

void CameraCore::OnEntityAdded( Entity& NewEntity )
{
    if( Camera::CurrentCamera == DefaultCamera )
    {
        NewEntity.GetComponent<Camera>().SetCurrent();
    }
    auto& renderer = GetEngine().GetRenderer();
    Moonlight::CameraData camData = CreateCameraData( NewEntity.GetComponent<Transform>(), NewEntity.GetComponent<Camera>() );
    unsigned int id = renderer.GetCameraCache().Push( camData );
    NewEntity.GetComponent<Camera>().m_id = id;
    renderer.RecreateFrameBuffer( id );
}

Moonlight::CameraData CameraCore::CreateCameraData( Transform& InTransform, Camera& InCamera )
{
    Moonlight::CameraData CamData;

    CamData.Position = InTransform.GetWorldPosition();
    CamData.Front = InTransform.Front();
    CamData.Up = InTransform.Up();
    if( InCamera.OutputSize.IsZero() )
    {
        InCamera.OutputSize = GetEngine().GetWindow()->GetSize();
    }
    CamData.OutputSize = InCamera.OutputSize;
    CamData.FOV = InCamera.GetFOV();
    CamData.Near = InCamera.Near;
    CamData.Far = InCamera.Far;
    CamData.Skybox = InCamera.Skybox;
    CamData.ClearColor = InCamera.ClearColor;
    CamData.ClearType = InCamera.ClearType;
    CamData.Projection = InCamera.Projection;
    CamData.OrthographicSize = InCamera.OrthographicSize;
    CamData.IsMain = InCamera.IsMain();

    //Vector3 Right = CamData.Front.Cross(Vector3::Up).Normalized();
    //Vector3 Up = CamData.Front.Cross(Right).Normalized();

    //CamData.CameraFrustum = InCamera.CameraFrustum;
    CamData.Buffer = ME_NEW Moonlight::FrameBuffer( static_cast<uint32_t>( CamData.OutputSize.x ), static_cast<uint32_t>( CamData.OutputSize.y ) );

    return CamData;
}

void CameraCore::OnEntityRemoved( Entity& InEntity )
{
    unsigned int id = InEntity.GetComponent<Camera>().m_id;
    Moonlight::CameraData* camData = GetEngine().GetRenderer().GetCameraCache().Get( id );
    delete camData->Buffer;
    GetEngine().GetRenderer().GetCameraCache().Pop( id );
}
