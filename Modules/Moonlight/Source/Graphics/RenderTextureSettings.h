#pragma once
#include "Reflection/Reflection.h"
#include <string>

namespace Moonlight
{
    // How samples past a render texture's edges wrap.
    enum class RenderTextureWrap : uint8_t
    {
        Clamp = 0,
        Repeat,
        Mirror
    };

    // A .rendertexture asset: an image a camera renders into (Camera::TargetTexture) and materials
    // sample like any texture. The file is this struct as JSON and ships as is (there's no cooked twin).
    struct RenderTextureSettings
    {
        ME_REFLECTABLE( RenderTextureSettings )
    public:
        static constexpr const char* kExtension = "rendertexture";
        static constexpr int kMaxSize = 4096;

        // The camera renders at this size, so it is also the feed's resolution and cost.
        int Width = 640;
        int Height = 360;
        // A mip chain, regenerated after every render: screens seen small or at an angle stay smooth.
        bool Mips = true;
        // Clamp keeps the edges clean (a material's Tiling and Offset can still flip the image);
        // Repeat for materials that tile or scroll it.
        RenderTextureWrap Wrap = RenderTextureWrap::Clamp;

        void Clamp();
        // The asset's JSON; missing keys keep their defaults. False when the text isn't a JSON object.
        bool Parse( const std::string& InText );
        std::string Write() const;

        bool operator==( const RenderTextureSettings& InOther ) const
        {
            return Width == InOther.Width && Height == InOther.Height && Mips == InOther.Mips && Wrap == InOther.Wrap;
        }
        bool operator!=( const RenderTextureSettings& InOther ) const
        {
            return !( *this == InOther );
        }

        // Whether a path names a render texture asset (by its extension).
        static bool IsRenderTexturePath( const std::string& InPath );
    };
}
