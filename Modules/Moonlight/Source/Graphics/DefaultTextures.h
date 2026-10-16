#pragma once
#include <bgfx/bgfx.h>
#include <string>

namespace Moonlight
{
    // 1x1 textures the renderer creates at startup, for materials that must bind every sampler they
    // declare (an unbound sampler is undefined behaviour, and crashes lavapipe).
    enum class DefaultTexture : unsigned int
    {
        White = 0,
        Black,
        Gray,
        FlatNormal,
        BlackCube,
        Count
    };

    bgfx::TextureHandle GetDefaultTexture( DefaultTexture InTexture );
    void SetDefaultTexture( DefaultTexture InTexture, bgfx::TextureHandle InHandle );

    // "white" / "black" / "gray" / "normal" / "cube" (anything else is white).
    DefaultTexture DefaultTextureFromName( const std::string& InName );
    const char* GetDefaultTextureName( DefaultTexture InTexture );
}
