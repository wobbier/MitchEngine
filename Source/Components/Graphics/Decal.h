#pragma once
#include "ECS/Component.h"
#include "ECS/ComponentDetail.h"
#include "RenderCommands.h"
#include "Path.h"
#include "Pointers.h"
#include "Math/Matrix4.h"
#include "Math/Vector3.h"
#include <string>

namespace Moonlight { class Texture; }
class Transform;

// Projects a texture onto the opaque surfaces inside a box (scorch marks, puddles, graffiti, blood,
// signage, road paint). The box is the entity's transform scaled by Size: the texture's right is the
// entity's X axis, its up the Y axis, and it projects along forward (Z), reaching Size.z deep. The
// decal is lit like the surface it lands on (its own roughness, metalness and normal map), lands
// before ambient occlusion and fog, and fades out on surfaces turned away from the projection.
class Decal
    : public Component<Decal>
{
    ME_REFLECTABLE( Decal )
public:
    Decal();

    // sRGB colour (alpha is coverage); empty projects a solid Color.
    Path Albedo;
    // Tangent-space normal map; empty uses the surface normal reconstructed from depth.
    Path NormalMap;
    // sRGB tint multiplied into the albedo.
    Vector3 Color { 1.f, 1.f, 1.f };
    float Opacity = 1.f;
    float Metallic = 0.f;
    float Roughness = 0.6f;
    float NormalStrength = 1.f;
    // sRGB colour of emitted light (glowing runes, neon paint), scaled by EmissiveIntensity.
    Vector3 EmissiveColor { 1.f, 1.f, 1.f };
    float EmissiveIntensity = 0.f;
    // Degrees: surfaces turned further than this from facing the projector get none of the decal, and
    // it fades in over AngleFadeWidth degrees before that.
    float AngleFade = 70.f;
    float AngleFadeWidth = 20.f;
    // Metres: the projection box (x wide, y tall, z deep), on top of the entity's scale.
    Vector3 Size { 1.f, 1.f, 1.f };
    // Overlapping decals draw in ascending order, so higher values land on top.
    int SortOrder = 0;
    // A Decal shader graph for the projected surface (replaces Albedo / NormalMap / the surface values).
    Path Graph;

    // Starts loading the textures (when they change) and reports whether the decal can draw: it waits
    // for its albedo to stream in rather than flash a solid tint.
    bool Resolve();
    // This frame's render command, from the entity's world transform.
    Moonlight::DecalCommand BuildCommand( Transform& InTransform ) const;
    // The projection box in world space.
    static Matrix4 BoxMatrix( Transform& InTransform, const Vector3& InSize );

private:
    SharedPtr<Moonlight::Texture> m_albedo;
    SharedPtr<Moonlight::Texture> m_normal;
    std::string m_loadedAlbedo;
    std::string m_loadedNormal;
    SharedPtr<Moonlight::ShaderGraphAsset> m_graph;
    std::string m_loadedGraph;
};
ME_REGISTER_COMPONENT_FOLDER( Decal, "Rendering" )
