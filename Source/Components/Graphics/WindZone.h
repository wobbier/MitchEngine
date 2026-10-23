#pragma once
#include "ECS/Component.h"
#include "ECS/ComponentDetail.h"
#include "RenderCommands.h"

// The scene's wind: generated trees (FoliageMaterial) lean, sway and flutter in it. It blows along
// the entity's forward axis (flattened to horizontal); the first active zone in the scene is used,
// and without one foliage moves in a light breeze.
class WindZone
    : public Component<WindZone>
{
    ME_REFLECTABLE( WindZone )
public:
    WindZone();

    // This frame's wind (direction from the transform).
    Moonlight::WindCommand BuildCommand( const class Transform& InTransform ) const;

    float Strength = 0.6f;          // 0 calm, 1 a strong breeze, 2+ a storm
    float Gusts = 0.5f;
    float GustFrequency = 0.15f;    // gust fronts per second
    float Turbulence = 0.3f;

#if USING( ME_EDITOR )
    void OnEditorInspect() override;
#endif
};

ME_REGISTER_COMPONENT_FOLDER( WindZone, "Rendering" )
