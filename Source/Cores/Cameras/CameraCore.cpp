#include "PCH.h"
#include "CameraCore.h"
#include "Components/Camera.h"
#include "Components/Cameras/CameraSway.h"
#include "Engine/Engine.h"
#include "Window/IWindow.h"
#include "Math/Frustrum.h"
#include "Camera/CameraData.h"
#include "Renderer.h"
#include "Mathf.h"
#include "Components/Graphics/PostProcess.h"

CameraCore::CameraCore() : Base( ComponentFilter().Requires<Camera>().Requires<Transform>() )
{
    SetIsSerializable( false );
    DefaultCamera = new Camera();
}

CameraCore::~CameraCore()
{
    delete DefaultCamera;
}

void CameraCore::Init()
{
    DefaultCamera->SetCurrent();
}

void CameraCore::Update( const UpdateContext& inUpdateContext )
{
}


void CameraCore::OnStart()
{
    m_running = true;
    for( Entity& entity : GetEntities() )
    {
        entity.GetComponent<Camera>().ResetSkyClock();
    }
}


void CameraCore::OnStop()
{
    m_running = false;
    for( Entity& entity : GetEntities() )
    {
        entity.GetComponent<Camera>().ResetSkyClock();
        if( CameraSway* sway = entity.TryGetComponent<CameraSway>() )
        {
            sway->Reset();
        }
    }
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
        // Render texture cameras render at their target's size (null until it has loaded). Resolved
        // for every camera, so clearing the field lets the texture go.
        Moonlight::Texture* resolved = CameraComponent.ResolveTargetTexture();
        const bool toTexture = CameraComponent.RendersToTexture();
        Moonlight::Texture* target = toTexture ? resolved : nullptr;

#if USING( ME_EDITOR )
        // The editor sizes the main camera to the Game View panel; only fall back to the window
        // until it has, otherwise the two fight and the camera renders with the window's size.
        if( CameraComponent.IsMain() && CameraComponent.OutputSize.IsZero() )
#else
        if( CameraComponent.IsMain() )
#endif
        {
            CameraComponent.OutputSize = windowSize;
        }
        else if( toTexture )
        {
            if( target )
            {
                CameraComponent.OutputSize = Vector2( static_cast<float>( target->mWidth ), static_cast<float>( target->mHeight ) );
            }
        }
        else if( CameraComponent.MatchMainCamera && Camera::CurrentCamera )
        {
            // Screen-space views (PortalMaterial) line up pixel for pixel with the main camera and share
            // its background.
            CameraComponent.OutputSize = Camera::CurrentCamera->OutputSize;
            CameraComponent.m_FOV = Camera::CurrentCamera->m_FOV;
            CameraComponent.Far = Camera::CurrentCamera->Far;
            CameraComponent.Projection = Camera::CurrentCamera->Projection;
            CameraComponent.OrthographicSize = Camera::CurrentCamera->OrthographicSize;
            CameraComponent.ClearType = Camera::CurrentCamera->ClearType;
            CameraComponent.ClearColor = Camera::CurrentCamera->ClearColor;
            CameraComponent.Skybox = Camera::CurrentCamera->Skybox;
            CameraComponent.ProceduralSky = Camera::CurrentCamera->ProceduralSky;
        }
        else if( CameraComponent.HasPartialViewport() )
        {
            // Split screen cameras share the main camera's output, so size from it.
            CameraComponent.OutputSize = Camera::CurrentCamera ? Camera::CurrentCamera->OutputSize : windowSize;
        }

        Moonlight::CameraData* CamData = cameraCache.Get( CameraComponent.m_id );
        if( CamData )
        {
            // The viewport reapplies its override after camera updates. Cameras switched out of
            // Game View return to lit rendering (for example when used as a render texture).
            CamData->Shading = Moonlight::ViewShadingMode::Lit;
            CamData->ViewOptions = Moonlight::ViewRenderOptions();
            CamData->Position = TransformComponent.GetWorldPosition();
            CamData->Front = TransformComponent.Front();
            CamData->Up = TransformComponent.Up();
            if( CameraSway* sway = InEntity.TryGetComponent<CameraSway>() )
            {
                if( m_running && sway->IsEnabled() )
                {
                    sway->Advance( inUpdateContext.GetDeltaTime() );
                    Quaternion rotation = TransformComponent.GetWorldRotation();
                    sway->Apply( CamData->Position, rotation );
                    CamData->Front = rotation * Vector3::Front;
                    CamData->Up = rotation * Vector3::Up;
                }
                else
                {
                    sway->Reset();
                }
            }
            const Vector2 viewportSize = CameraComponent.GetViewportSize();
            CamData->OutputSize = { Mathf::Abs( viewportSize.x ), Mathf::Abs( viewportSize.y ) };
            CamData->Viewport = CameraComponent.HasPartialViewport() ? CameraComponent.Viewport.InternalVector : glm::vec4( 0.f, 0.f, 1.f, 1.f );

            CamData->FOV = CameraComponent.GetFOV();
            CamData->Near = CameraComponent.Near;
            CamData->Far = CameraComponent.Far;
            CamData->Skybox = CameraComponent.Skybox;
            CamData->ClearColor = CameraComponent.ClearColor;
            CamData->ClearType = CameraComponent.ClearType;
            if( !m_running ) CameraComponent.ResetSkyClock();
            CamData->ProceduralSky = CameraComponent.GetSkySettings( m_running ? inUpdateContext.GetDeltaTime() : 0.f );
            CamData->Projection = CameraComponent.Projection;
            CamData->OrthographicSize = CameraComponent.OrthographicSize;
            CamData->IsMain = CameraComponent.IsMain();
            const PostProcess* post = InEntity.TryGetComponent<PostProcess>();
            CamData->Post = ( post && post->IsEnabled() ) ? post->ToSettings() : Moonlight::PostProcessSettings();
            CamData->SunShadows = CameraComponent.SunShadows;
            CamData->AmbientOcclusion = CameraComponent.AmbientOcclusion;
            CamData->VolumetricFog = CameraComponent.VolumetricFog;
            CamData->Target = target ? CameraComponent.m_target : nullptr;
            CamData->TargetOrder = InEntity.GetGUID();
            CamData->RenderDeltaSeconds = 0.f;
            if( toTexture )
            {
                // Render textures render on their own schedule, counted in frames so captures stay
                // deterministic: every UpdateInterval frames, and at once for a new or recreated target.
                // Without a target there's nothing to render into.
                CamData->ShouldRender = false;
                CameraComponent.m_secondsSinceRender += inUpdateContext.GetDeltaTime();
                if( target )
                {
                    const bool retarget = target != CameraComponent.m_scheduledTarget || target->GetRenderTargetRevision() != CameraComponent.m_targetRevision;
                    if( retarget || CameraComponent.m_framesUntilRender <= 0 )
                    {
                        CamData->ShouldRender = true;
                        CamData->RenderDeltaSeconds = CameraComponent.m_secondsSinceRender;
                        CameraComponent.m_secondsSinceRender = 0.f;
                        const int interval = std::max( CameraComponent.UpdateInterval, 1 );
                        CameraComponent.m_framesUntilRender = interval - 1;
                        if( retarget )
                        {
                            // A new target renders at once; then feeds sharing an interval take turns
                            // (by GUID, so it's the same every run).
                            CameraComponent.m_framesUntilRender -= static_cast<int>( InEntity.GetGUID() % static_cast<uint64_t>( interval ) );
                        }
                        CameraComponent.m_scheduledTarget = target;
                        CameraComponent.m_targetRevision = target->GetRenderTargetRevision();
                    }
                    else
                    {
                        --CameraComponent.m_framesUntilRender;
                    }
                }
                // Nothing editor-only or screen-only belongs in a texture.
                CamData->ViewOptions.DebugDraw = false;
                CamData->ViewOptions.GameUI = false;
                CameraComponent.m_renderedToTexture = true;
            }
            else if( CameraComponent.m_renderedToTexture )
            {
                // Back to an ordinary camera (target cleared, or made current: the Game View then
                // decides for the current camera every frame).
                CamData->ShouldRender = true;
                CameraComponent.m_renderedToTexture = false;
                CameraComponent.m_scheduledTarget = nullptr;
            }
            //CamData.CameraFrustum = CameraComponent.CameraFrustum;

            Vector3 eye = { CamData->Position.x, CamData->Position.y, CamData->Position.z };
            Vector3 at = { CamData->Position.x + CamData->Front.x, CamData->Position.y + CamData->Front.y, CamData->Position.z + CamData->Front.z };
            Vector3 up = { CamData->Up.x, CamData->Up.y, CamData->Up.z };

            Frustum& camFrustum = CameraComponent.CameraFrustum;
            //bx::mtxProj( &CameraComponent.WorldToCamera.GetInternalMatrix()[0][0], CameraComponent.GetFOV(), float( CameraComponent.OutputSize.x ) / float( CameraComponent.OutputSize.y ), std::max( CameraComponent.Near, 1.f ), CameraComponent.Far, bgfx::getCaps()->homogeneousDepth );
            CamData->View = glm::lookAtLH( eye.InternalVector, at.InternalVector, up.InternalVector );
            if( CameraComponent.m_hasClipPlane )
            {
                // The world plane in view space, visible side positive. A camera in front of the
                // plane would clip what it should see, so it renders normally instead.
                const glm::mat4& view = CamData->View.GetInternalMatrix();
                const glm::vec3 normal = glm::normalize( glm::mat3( view ) * CameraComponent.m_clipNormal.InternalVector );
                const glm::vec3 point = glm::vec3( view * glm::vec4( CameraComponent.m_clipPoint.InternalVector, 1.f ) );
                const glm::vec4 plane( normal, -glm::dot( normal, point ) );
                CameraComponent.isOblique = plane.w < 0.f;
                CameraComponent.ObliqueMatData = plane;
            }
            CamData->IsOblique = CameraComponent.isOblique;
            CamData->ObliqueData = CameraComponent.ObliqueMatData;


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
    // A render texture camera never takes over the screen by itself.
    if( Camera::CurrentCamera == DefaultCamera && !NewEntity.GetComponent<Camera>().HasTargetTexture() )
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
    CamData.ProceduralSky = InCamera.GetSkySettings();
    CamData.Projection = InCamera.Projection;
    CamData.OrthographicSize = InCamera.OrthographicSize;
    CamData.IsMain = InCamera.IsMain();

    //Vector3 Right = CamData.Front.Cross(Vector3::Up).Normalized();
    //Vector3 Up = CamData.Front.Cross(Right).Normalized();

    //CamData.CameraFrustum = InCamera.CameraFrustum;
    CamData.Buffer = new Moonlight::FrameBuffer( static_cast<uint32_t>( CamData.OutputSize.x ), static_cast<uint32_t>( CamData.OutputSize.y ) );

    return CamData;
}

void CameraCore::OnEntityRemoved( Entity& InEntity )
{
    Camera& camera = InEntity.GetComponent<Camera>();
    const unsigned int id = camera.m_id;
    Moonlight::CameraData* camData = GetEngine().GetRenderer().GetCameraCache().Get( id );
    delete camData->Buffer;
    GetEngine().GetRenderer().GetCameraCache().Pop( id );
    // The slot goes to the next camera added: this one (deactivated, perhaps still current) has none.
    camera.m_id = ~0u;
}
