#include "DefaultTextures.h"

namespace Moonlight
{
    namespace
    {
        bgfx::TextureHandle s_defaultTextures[static_cast<unsigned int>( DefaultTexture::Count )] = {
            BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE
        };
    }


    bgfx::TextureHandle GetDefaultTexture( DefaultTexture InTexture )
    {
        return s_defaultTextures[static_cast<unsigned int>( InTexture )];
    }


    void SetDefaultTexture( DefaultTexture InTexture, bgfx::TextureHandle InHandle )
    {
        s_defaultTextures[static_cast<unsigned int>( InTexture )] = InHandle;
    }


    DefaultTexture DefaultTextureFromName( const std::string& InName )
    {
        if( InName == "black" )
        {
            return DefaultTexture::Black;
        }
        if( InName == "gray" || InName == "grey" )
        {
            return DefaultTexture::Gray;
        }
        if( InName == "normal" )
        {
            return DefaultTexture::FlatNormal;
        }
        if( InName == "cube" )
        {
            return DefaultTexture::BlackCube;
        }
        return DefaultTexture::White;
    }


    const char* GetDefaultTextureName( DefaultTexture InTexture )
    {
        switch( InTexture )
        {
        case DefaultTexture::Black:
            return "black";
        case DefaultTexture::Gray:
            return "gray";
        case DefaultTexture::FlatNormal:
            return "normal";
        case DefaultTexture::BlackCube:
            return "cube";
        case DefaultTexture::White:
        default:
            return "white";
        }
    }
}
