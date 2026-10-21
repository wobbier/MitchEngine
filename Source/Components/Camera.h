#pragma once

#include "ECS/Component.h"
#include "ECS/ComponentDetail.h"
#include "Components/Transform.h"
#include "Utils/HavanaUtils.h"
#include "Graphics/SkyBox.h"
#include "Camera/CameraData.h"
#include "Math/Frustrum.h"

//class Frustum;

class Camera
    : public Component<Camera>
{
    ME_REFLECTABLE( Camera )
    friend class CameraCore;
public:
    inline static Camera* CurrentCamera = nullptr;
    inline static Camera* EditorCamera = nullptr;

    Vector2 OutputSize;
    Vector3 ClearColor;
    float Zoom = 45.0f;
    float Yaw = 0.f;
    float Pitch = 0.f;
    float Roll = 0.f;
    // Distances are metres. OrthographicSize is half the view height (width follows the aspect).
    float OrthographicSize = 5.f;
    float Near = 0.1f;
    float Far = 1000.f;
    // Normalized screen rect (x, y from the top left, width, height) for split screen. A camera with
    // a partial rect draws into that part of the main camera's output instead of its own target.
    Vector4 Viewport = Vector4( 0.f, 0.f, 1.f, 1.f );
    // Render at the main camera's output size, field of view and far plane, with its background, for
    // views shown in screen space on a surface (PortalMaterial).
    bool MatchMainCamera = false;

    Camera();
    ~Camera();

    virtual void Init() override;
    void OnPropertyChanged( const std::string& InFieldName ) override;

    Matrix4 GetViewMatrix();
    bool IsCurrent();
    void SetCurrent();
    float GetFOV();
    float GetAspectRatio() const;
    bool HasPartialViewport() const;
    // OutputSize scaled by the viewport rect: the size this camera actually renders at.
    Vector2 GetViewportSize() const;

    const int GetCameraId() const;

    const bool IsMain() const;

    void SetObliqueMatrixData( const glm::vec4& inVec );
    void ClearObliqueMatrixData();

    // Clips everything behind a world-space plane (the side the normal points away from) by
    // replacing the near plane with it: what a camera looking out of a portal or mirror needs.
    // Ignored while the camera itself is in front of the plane.
    void SetClipPlane( const Vector3& InPoint, const Vector3& InNormal );
    void ClearClipPlane();

    Frustum CameraFrustum;
    Moonlight::SkyBox* Skybox = nullptr;
    Moonlight::ProjectionType Projection = Moonlight::ProjectionType::Perspective;
    Moonlight::ClearColorType ClearType = Moonlight::ClearColorType::Color;
    Moonlight::ProceduralSkySettings ProceduralSky;

    // Advance once per update; other views can read the same clock with a zero delta.
    Moonlight::ProceduralSkySettings GetSkySettings( float deltaTime = 0.f );
    // Discard playback state without changing the authored sky values.
    void ResetSkyClock();

    Matrix4 WorldToCamera;

    Matrix4 GetProjectionMatrix() const;

#if USING( ME_EDITOR )
    virtual void OnEditorInspect() final;
#endif

private:
    float m_skyTime = 15.f;
    float m_skyStartTime = 15.f;
    float m_moonPhase = 0.4f;
    float m_moonStartPhase = 0.4f;
    float m_skyAnimationTime = 0.f;
    float m_FOV = 60.f;
    unsigned int m_id = 0;
    glm::vec4 ObliqueMatData;
    bool isOblique = false;
    bool m_hasClipPlane = false;
    Vector3 m_clipPoint;
    Vector3 m_clipNormal;
    virtual void OnDeserialize( const json& inJson ) final;
    virtual void OnSerialize( json& outJson ) final;
};
ME_REGISTER_COMPONENT_FOLDER( Camera, "Rendering" )
