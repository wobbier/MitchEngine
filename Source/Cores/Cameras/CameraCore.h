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

private:
    void OnStart() override;
    void OnStop() override;

    bool m_running = false;
    class Camera* DefaultCamera;
};
