#pragma once
#include "ECS/Component.h"
#include "ECS/ComponentDetail.h"
#include "RenderCommands.h"

// A light source. Directional lights shine along the entity's forward axis (the first shadowed one
// is the "sun" with cascaded shadows); point lights radiate from the entity's position out to
// Range; spot lights are cones along forward with inner/outer angles. Colours are sRGB, intensity
// is an HDR multiplier.
class Light
    : public Component<Light>
{
    ME_REFLECTABLE( Light )
public:
    Light();
    ~Light() = default;

    virtual void Init() final;

    // Fills a render command for this frame (world-space position/direction from the transform).
    Moonlight::LightCommand BuildCommand( const class Transform& InTransform ) const;

    Moonlight::LightType LightType = Moonlight::LightType::Point;
    Vector3 Color { 1.f, 1.f, 1.f };
    float Intensity = 1.f;
    float Range = 10.f;
    float InnerConeAngle = 20.f;
    float OuterConeAngle = 30.f;
    bool CastShadows = false;
    float ShadowBias = 0.0005f;
    float ShadowNormalBias = 0.02f;

private:
    void OnDeserialize( const json& inJson ) override;
};
ME_REGISTER_COMPONENT_FOLDER( Light, "Rendering" )
