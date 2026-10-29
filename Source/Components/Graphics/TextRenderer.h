#pragma once
#include "ECS/Component.h"
#include "ECS/ComponentDetail.h"
#include "RenderCommands.h"
#include "Path.h"
#include "Math/Vector3.h"
#include <glm/glm.hpp>
#include <string>
#include <vector>

class Transform;

enum class TextAlignment : uint8_t
{
    Left = 0,
    Center,
    Right,
};

enum class TextAnchor : uint8_t
{
    Top = 0,
    Middle,
    Bottom,
};

// World-space text (LOOK-S12): signs, labels, damage numbers. Lines of UTF-8 (\n breaks) laid out
// in the entity's local XY plane, facing local -Z like sprites (or turned to face the main
// camera with Billboard), drawn through the sprite pass as signed-distance glyphs, so it stays
// crisp at any size. Size is the line height in world units.
class TextRenderer
    : public Component<TextRenderer>
{
    ME_REFLECTABLE( TextRenderer )
public:
    TextRenderer();

    std::string Text = "Text";
    // A .ttf / .otf; empty = Engine/Assets/Fonts/Inter-Regular.ttf.
    Path Font;
    float Size = 0.5f;
    float LineSpacing = 1.f;
    TextAlignment Alignment = TextAlignment::Center;
    TextAnchor Anchor = TextAnchor::Middle;
    Vector3 Color = Vector3( 1.f, 1.f, 1.f );
    float Opacity = 1.f;
    bool Billboard = false;
    bool Lit = false;
    int SortingLayer = 0;
    int OrderInLayer = 0;

    // One sprite command per visible glyph. InCameraRotation: the main camera's world rotation
    // (billboards turn to it; identity when there's no camera).
    void BuildCommands( Transform& InTransform, const glm::mat3& InCameraRotation, std::vector<Moonlight::SpriteCommand>& OutCommands, uint64_t InEntityId ) const;
    // The laid-out block's width and height in world units (0 when the font can't load).
    glm::vec2 Measure() const;
};
ME_REGISTER_COMPONENT_FOLDER( TextRenderer, "Rendering" )
