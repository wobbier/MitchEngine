#pragma once
#include "ECS/Core.h"
#include "Events/EventReceiver.h"
#include "Components/Camera.h"

class TestEvent
    : public Event<TestEvent>
{
public:
    bool Enabled = true;
};

class FlyingCameraCore
    : public Core<FlyingCameraCore>
    , public EventReceiver
{
public:
    FlyingCameraCore();
    ~FlyingCameraCore();

    // Separate init from construction code.
    virtual void Init() final;

    // Each core must update each loop
    virtual void Update( float dt ) final;

    virtual bool OnEvent( const BaseEvent& evt );

private:
    float LastX = 0.f;
    float LastY = 0.f;
    bool FirstUpdate = true;
    bool InputEnabled = false;
    bool PreviousMouseDown = false;

#if USING( ME_EDITOR )

    virtual void OnEditorInspect() final;

#endif
public:
    void SetCamera( Camera* NewCam )
    {
        Cam = NewCam;
    }
    Camera* Cam = nullptr;
};
