#pragma once
#include "Graphics/SpriteSheet.h"
#include <cstdint>
#include <string>
#include <vector>

// Sprite atlases (ME-277): many sprite images packed into one texture, so the sprites batch.
//
// A .spriteatlas is JSON: { "Sources": [ ... ], "Padding": 2, "MaxSize": 2048, "Extrude": 1 }.
// A source is an image, a folder of images (not recursive), or a .spritesheet whose sprites each
// join (named "<sheet>/<sprite>"); an image's sprite is named after the file. The cooker packs the
// atlases a cook's sprites use (Docs/Cooking.md), and shipped SpriteRenderers draw from the atlas.
namespace Moonlight
{
    struct SpriteAtlasDesc
    {
        static constexpr const char* kExtension = "spriteatlas";

        std::vector<std::string> Sources;
        int Padding = 2;        // texels between sprites
        int MaxSize = 2048;     // the atlas's largest side
        int Extrude = 1;        // edge texels repeated outwards, so filtering doesn't bleed

        bool Parse( const std::string& InText );
        std::string Write() const;
    };

    // MaxRects, best short side fit, no rotation. Rects are placed in order of decreasing height,
    // then width, then index, so a pack is deterministic. Each rect's X/Y are filled in; OutWidth /
    // OutHeight are the smallest powers of two holding them. False when they don't fit InMaxSize.
    struct PackRect
    {
        int Width = 0;
        int Height = 0;
        int X = 0;
        int Y = 0;
    };
    bool PackMaxRects( std::vector<PackRect>& InOutRects, int InMaxSize, int& OutWidth, int& OutHeight );

    // One sprite going into an atlas: an RGBA8 image and the part of it that's the sprite.
    struct AtlasInput
    {
        std::string Name;
        std::string SourceTexture;      // where it came from (the cooker's map)
        std::string SourceSheet;        // and, for a sheet's sprite, the sheet and sprite
        std::string SourceSprite;
        const uint8_t* Rgba = nullptr;  // the whole source image
        int ImageWidth = 0;
        int ImageHeight = 0;
        glm::vec4 Rect = glm::vec4( 0.f );  // x, y, w, h in the image (zero size: all of it)
        glm::vec2 Pivot = glm::vec2( 0.5f );
        glm::vec4 Border = glm::vec4( 0.f );
    };

    struct SpriteAtlasImage
    {
        int Width = 0;
        int Height = 0;
        std::vector<uint8_t> Rgba;
        // In atlas texels, one per input, in input order (Name, pivot and border carried over).
        std::vector<SpriteFrame> Frames;
    };

    // Packs and copies the inputs (pure: no files). False (OutError) when they don't fit.
    bool ComposeSpriteAtlas( const std::vector<AtlasInput>& InInputs, const SpriteAtlasDesc& InDesc, SpriteAtlasImage& OutImage, std::string& OutError );

    // A .spriteatlas's sources, read through the VFS, composed; OutInputs keeps where each sprite
    // came from (its pixel pointers aren't valid afterwards).
    bool BuildSpriteAtlas( const std::string& InAtlasPath, SpriteAtlasImage& OutImage, std::vector<AtlasInput>& OutInputs, std::string& OutError );
    // Writes the atlas as <InBase>.png and <InBase>.spritesheet (InBase is a project path, without
    // an extension; files go to its full path).
    bool WriteSpriteAtlas( const SpriteAtlasImage& InImage, const std::string& InBase, std::string& OutError );

    // The editor's Pack (ME-282): builds a .spriteatlas and writes <atlas>.png / .spritesheet beside
    // it, as the cooker would. OutImage (optional) receives the packed image. -1 on failure.
    int PackSpriteAtlasFile( const std::string& InAtlasPath, std::string& OutError, SpriteAtlasImage* OutImage = nullptr );
}

// The .spriteatlas inspector (ME-282): its sources, padding, size and extrusion, a Pack button and
// a preview of the packed image with every sprite's rectangle outlined.
struct SpriteAtlasMetadata
    : public MetaBase
{
    explicit SpriteAtlasMetadata( const Path& InPath )
        : MetaBase( InPath )
    {
    }

    std::string GetExtension2() const override
    {
        return Moonlight::SpriteAtlasDesc::kExtension;
    }
    void OnSerialize( json& OutJson ) override
    {
    }
    void OnDeserialize( const json& InJson ) override
    {
    }

#if USING( ME_EDITOR )
    void OnEditorInspect() override;
#endif
};
ME_REGISTER_METADATA( "spriteatlas", SpriteAtlasMetadata );
