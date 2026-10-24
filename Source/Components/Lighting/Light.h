#pragma once
#include "ECS/Component.h"
#include "ECS/ComponentDetail.h"
#include "RenderCommands.h"

// A light source. Directional lights follow procedural sky bodies through DirectionalSource;
// Automatic supplies sun and moon together, and Manual opts a light out — it keeps the entity's
// forward axis, colour and intensity alongside the sky. Without a procedural sky they use the
// entity's forward axis. The first visible shadow caster uses cascaded shadows. Point lights radiate from
// the entity's position out to
// Range; spot lights are cones along forward with inner/outer angles. Bars emit along a capsule on
// local X (for fluorescent tubes); boxes emit from a local cuboid; rectangles emit from a local XY
// face into the forward (+Z) hemisphere. Extended emitters follow world
// rotation/scale; Range measures the fade distance from their surface. Colours are sRGB, intensity
// is an HDR multiplier. Extended lighting uses a closest-source approximation, softened near the
// emitter by its projected area so it stays finite on its surface, and has no shadows.
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
    Moonlight::DirectionalSource DirectionalSource = Moonlight::DirectionalSource::Automatic;
    Vector3 Color { 1.f, 1.f, 1.f };
    float Intensity = 1.f;
    float Range = 10.f;
    float InnerConeAngle = 20.f;
    float OuterConeAngle = 30.f;
    float Length = 1.2f;
    float Radius = 0.025f;
    Vector3 Size { 1.f, 0.2f, 0.2f };
    float RectangleWidth = 2.f;
    float RectangleHeight = 1.f;
    bool CastShadows = false;
    // In shadow-map texels: depth bias pushes receivers towards the light, normal bias along their
    // surface normal. Raise them for acne, lower them for detached ("peter panning") shadows.
    float ShadowBias = 0.5f;
    float ShadowNormalBias = 2.5f;
    // Directional lights: shadows cover the view out to this distance (split over 4 cascades).
    float ShadowDistance = 80.f;

private:
    void OnDeserialize( const json& inJson ) override;
};
ME_REGISTER_COMPONENT_FOLDER( Light, "Rendering" )
