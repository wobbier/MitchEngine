#pragma once
#include "Resource/Resource.h"
#include "Resource/MetaFile.h"
#include "Resource/MetaRegistry.h"
#include "JSON.h"
#include "Reflection/Reflection.h"
#include "Scene/AnimationClip.h"
#include <glm/glm.hpp>
#include <cstdint>
#include <string>
#include <vector>

namespace Moonlight
{
    // One named sprite of a sheet: a rectangle in texels (x, y from the image's top left), its
    // pivot (0, 0 bottom left) and 9-slice borders (left, top, right, bottom texels; zero = none).
    struct SpriteFrame
    {
        std::string Name;
        glm::vec4 Rect = glm::vec4( 0.f );
        glm::vec2 Pivot = glm::vec2( 0.5f );
        glm::vec4 Border = glm::vec4( 0.f );
    };

    enum class SpriteSliceMode : uint8_t
    {
        Grid = 0,   // fixed cells
        Auto,       // one sprite per island of opaque texels
    };

    // How the editor slices a sheet's image (kept in the sheet so a re-slice repeats it).
    struct SpriteSlicing
    {
        SpriteSliceMode Mode = SpriteSliceMode::Grid;
        int CellWidth = 32;
        int CellHeight = 32;
        int OffsetX = 0;          // the first cell's top left
        int OffsetY = 0;
        int PaddingX = 0;         // gaps between cells
        int PaddingY = 0;
        int AlphaThreshold = 1;   // texels with at least this alpha (0..255) are opaque
        bool SkipEmpty = true;    // Grid: leave out cells with no opaque texel
        glm::vec2 Pivot = glm::vec2( 0.5f );
    };

    // A .spritesheet asset (LOOK-S3): an image cut into named sprites. JSON; it ships as is (cooks
    // copy it like the data it is), and SpriteRenderer names one of its sprites. Editing the file
    // (the editor's sheet inspector, or by hand) hot reloads every sprite using it.
    //   { "Texture": "Assets/Textures/Hero.png", "Slicing": { ... },
    //     "Sprites": [ { "Name": "Hero_0", "Rect": [0, 0, 32, 32], "Pivot": [0.5, 0], "Border": [0, 0, 0, 0] } ] }
    class SpriteSheet final
        : public Resource
    {
    public:
        static constexpr const char* kExtension = "spritesheet";

        explicit SpriteSheet( const Path& InPath );

        bool Load() override;
        void Reload() override;

        std::string Texture;
        SpriteSlicing Slicing;
        std::vector<SpriteFrame> Sprites;

        const SpriteFrame* Find( const std::string& InName ) const;
        // Bumped by every (re)load, so users notice a changed sheet.
        uint32_t GetRevision() const { return m_revision; }

        // The JSON form (Parse keeps the defaults for missing keys; false when it isn't an object).
        bool Parse( const std::string& InText );
        std::string Write() const;

        // Slicing an RGBA8 image (pure: no files). Sprites are named InBaseName_0, _1, ... in
        // reading order (rows top to bottom, then left to right).
        static std::vector<SpriteFrame> SliceGrid( const uint8_t* InRgba, int InWidth, int InHeight, const SpriteSlicing& InSlicing, const std::string& InBaseName );
        static std::vector<SpriteFrame> SliceAuto( const uint8_t* InRgba, int InWidth, int InHeight, const SpriteSlicing& InSlicing, const std::string& InBaseName );
        // Decodes an image file to RGBA8 through the VFS; false when it can't.
        static bool DecodeImage( const std::string& InPath, std::vector<uint8_t>& OutRgba, int& OutWidth, int& OutHeight );
        // Slices this sheet's Texture with its Slicing, replacing Sprites. Sprites whose name survives
        // keep their pivot and borders. False when the image can't be read.
        bool Slice();

    private:
        uint32_t m_revision = 0;
    };
}

namespace Moonlight
{
    // A .spriteanim asset (LOOK-S4): a flipbook of a sheet's sprites at a frame rate. An Animator's
    // SpriteClips turn each into a clip (named after the file) whose sprite keys drive the
    // SpriteRenderer on the Animator's entity, so states, transitions, blends and events work as
    // for any clip. JSON: { "Sheet": "...", "FramesPerSecond": 10, "Loop": true, "Frames": [ ... ] }.
    struct SpriteAnimation
    {
        ME_REFLECTABLE( SpriteAnimation )
    public:
        static constexpr const char* kExtension = "spriteanim";

        std::string Sheet;
        float FramesPerSecond = 10.f;
        bool Loop = true;
        std::vector<std::string> Frames;

        bool Parse( const std::string& InText );
        std::string Write() const;
        // The clip: one sprite key per frame, Duration = frames / FramesPerSecond.
        AnimationClip ToClip( const std::string& InName ) const;
        // Reads and converts the file at InPath (false when it can't).
        static bool LoadClip( const std::string& InPath, AnimationClip& OutClip );
    };
}

// Edited in the asset browser through reflection, like a .rendertexture.
struct SpriteAnimationMetadata
    : public MetaBase
{
    explicit SpriteAnimationMetadata( const Path& InPath )
        : MetaBase( InPath )
    {
    }

    std::string GetExtension2() const override
    {
        return Moonlight::SpriteAnimation::kExtension;
    }
    void OnSerialize( json& OutJson ) override
    {
    }
    void OnDeserialize( const json& InJson ) override
    {
    }

    const Reflection::TypeInfo* GetEditableType() override;
    void* GetEditableData() override;
    void SaveEditableData() override;

private:
    Moonlight::SpriteAnimation m_animation;
    bool m_loaded = false;
};
ME_REGISTER_METADATA( "spriteanim", SpriteAnimationMetadata );

// The meta only carries the asset's GUID; the inspector edits the sheet itself.
struct SpriteSheetMetadata
    : public MetaBase
{
    explicit SpriteSheetMetadata( const Path& InPath )
        : MetaBase( InPath )
    {
    }

    std::string GetExtension2() const override
    {
        return Moonlight::SpriteSheet::kExtension;
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
ME_REGISTER_METADATA( "spritesheet", SpriteSheetMetadata );
