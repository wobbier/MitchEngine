#pragma once
#include "ECS/Core.h"
#include "Camera/CameraData.h"
#include "Components/Camera.h"

class CameraCore
    : public Core<CameraCore>
{
public:
    CameraCore();
    ~CameraCore();

    // Separate init from construction code.
    virtual void Init() final;

    // Each core must update each loop
    virtual void Update( const UpdateContext& inUpdateContext ) final;
    virtual void LateUpdate( const UpdateContext& inUpdateContext ) final;

    virtual void OnEntityAdded( Entity& NewEntity ) final;

    Moonlight::CameraData CreateCameraData( Transform& InTransform, Camera& InCamera );

    void OnEntityRemoved( Entity& InEntity ) override;

    // ME-323: one world is presented a frame (a killcam's replay world, or the game world). A camera
    // core off screen marks its cameras neither main nor rendering, so the renderer presents the other
    // world's main camera; back on screen they render again from the next LateUpdate.
    void SetPresented( bool InPresented ) { m_presented = InPresented; }
    bool IsPresented() const { return m_presented; }

private:
    void OnStart() override;
    void OnStop() override;
    void OnRemovedFromWorld() override;
    void ForgetDefaultCamera();

    bool m_running = false;
    bool m_presented = true;
    bool m_wasHidden = false;
    class Camera* DefaultCamera;
};
