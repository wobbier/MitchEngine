#pragma once
#include "ECS/Core.h"
#include "Components/Camera.h"

// Free-flying cameras (FlyingCamera + Camera), for test and showcase scenes. Moves with the "Move"
// action (WASD / left stick in the default maps) or WASD without an action map, rises and sinks with
// E / Q or the shoulder buttons, sprints with Left Shift / the left stick press, and looks around
// while the right mouse button is held (the "Look" action's gamepad stick always looks).
class FlyingCameraCore
    : public Core<FlyingCameraCore>
{
public:
    FlyingCameraCore();

    void Update( const UpdateContext& InContext ) final;
};
