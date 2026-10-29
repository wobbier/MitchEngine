#pragma once
#include "ECS/Component.h"
#include "ECS/ComponentDetail.h"
#include "RenderCommands.h"
#include "Path.h"
#include "Pointers.h"
#include "Math/Vector2.h"
#include "Math/Vector3.h"
#include "Math/Vector4.h"
#include <string>

namespace Moonlight { class Texture; class SpriteSheet; }
class Transform;

// A 2D sprite (LOOK-S2): a textured quad in the entity's local XY plane, facing local -Z (towards a
// camera in front of it), in ortho or perspective. Region picks an atlas rectangle in texels,
// Pivot is the point the transform holds (0,0 bottom left, 1,1 top right), and the size comes from
// the region and PixelsPerUnit unless Size overrides it. Tint multiplies the texture (sRGB colour,
// Opacity alpha). SortingLayer, then OrderInLayer, decide what draws on top; equal ones sort back
// to front. NineSlice borders (texels) keep the corners unscaled while the middle stretches to
// Size: panels, buttons, frames. Lit sprites shade like a rough surface facing the viewer; unlit
// ones show the texture as is. Drawn after transparent meshes, depth-tested against the scene.
// With a Sheet (.spritesheet, LOOK-S3) and a SpriteName, the sheet's sprite supplies the region,
// pivot and 9-slice borders (and the texture, unless Texture names another), so re-slicing the
// sheet updates every sprite using it.
class SpriteRenderer
    : public Component<SpriteRenderer>
{
    ME_REFLECTABLE( SpriteRenderer )
public:
    SpriteRenderer();

    Path Texture;
    Path Sheet;
    std::string SpriteName;
    // x, y, width, height in texels from the image's top left; zero width or height = the whole image.
    Vector4 Region = Vector4( 0.f, 0.f, 0.f, 0.f );
    Vector2 Pivot = Vector2( 0.5f, 0.5f );
    float PixelsPerUnit = 100.f;
    // World size (units); zero = the region's size / PixelsPerUnit.
    Vector2 Size = Vector2( 0.f, 0.f );
    bool FlipX = false;
    bool FlipY = false;
    Vector3 Tint = Vector3( 1.f, 1.f, 1.f );
    float Opacity = 1.f;
    int SortingLayer = 0;
    int OrderInLayer = 0;
    // Left, top, right, bottom borders in texels (zero = a plain quad).
    Vector4 NineSlice = Vector4( 0.f, 0.f, 0.f, 0.f );
    bool Lit = false;
    // Nearest-texel sampling (pixel art).
    bool PointFilter = false;
    Moonlight::SpriteBlend Blend = Moonlight::SpriteBlend::Alpha;

    // Starts loading the texture when it changes; true when the sprite can draw (no texture draws
    // a white quad, a texture still streaming in waits).
    bool Resolve();
    // The texture's size in texels (1 x 1 without one).
    Vector2 GetTextureSize() const;
    // The world size the sprite draws at.
    Vector2 GetWorldSize() const;
    // This frame's render command, from the entity's world transform.
    Moonlight::SpriteCommand BuildCommand( Transform& InTransform ) const;

#if USING( ME_EDITOR )
    void OnEditorInspect() override;
#endif

    // The region, pivot and borders in effect: the sheet's sprite when there is one.
    void GetFrame( Vector4& OutRegion, Vector2& OutPivot, Vector4& OutBorder ) const;
    const Moonlight::SpriteSheet* GetSheet() const { return m_sheet.get(); }

private:
    SharedPtr<Moonlight::Texture> m_texture;
    std::string m_loadedTexture;
    SharedPtr<Moonlight::SpriteSheet> m_sheet;
    std::string m_loadedSheet;
};
ME_REGISTER_COMPONENT_FOLDER( SpriteRenderer, "Rendering" )
