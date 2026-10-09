#pragma once
#include "ECS/Component.h"
#include "ECS/ComponentDetail.h"
#include "RenderCommands.h"

class Light
    : public Component<Light>
{
    ME_REFLECTABLE( Light )
public:
    Light();
    ~Light() = default;

    // Separate init from construction code.
    virtual void Init() final;

    Vector3 Colour { 1.f, 1.f, 1.f };
    Moonlight::LightCommand cmd;
};
ME_REGISTER_COMPONENT_FOLDER( Light, "Rendering" )
