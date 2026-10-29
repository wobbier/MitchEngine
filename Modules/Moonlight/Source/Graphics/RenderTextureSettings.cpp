#include "RenderTextureSettings.h"
#include <algorithm>
#include <cctype>

ME_REFLECT_ENUM( Moonlight::RenderTextureWrap, { { "Clamp", Moonlight::RenderTextureWrap::Clamp }, { "Repeat", Moonlight::RenderTextureWrap::Repeat }, { "Mirror", Moonlight::RenderTextureWrap::Mirror } } )

ME_REFLECT_BEGIN( Moonlight::RenderTextureSettings )
    ME_FIELD( Width ).Range( 1.f, static_cast<float>( Moonlight::RenderTextureSettings::kMaxSize ) ).Tooltip( "Pixels across; the camera renders at this size" );
    ME_FIELD( Height ).Range( 1.f, static_cast<float>( Moonlight::RenderTextureSettings::kMaxSize ) ).Tooltip( "Pixels down; the camera's aspect ratio follows" );
    ME_FIELD( Mips ).Display( "Generate Mips" ).Tooltip( "Rebuild a mip chain after every render, so the image stays smooth when it's seen small or at an angle" );
    ME_FIELD( Wrap ).Tooltip( "How samples past the edges wrap: Clamp keeps them clean, Repeat suits materials that tile or scroll the image" );
ME_REFLECT_END()


namespace Moonlight
{
    void RenderTextureSettings::Clamp()
    {
        Width = std::clamp( Width, 1, kMaxSize );
        Height = std::clamp( Height, 1, kMaxSize );
    }


    bool RenderTextureSettings::Parse( const std::string& InText )
    {
        const json root = json::parse( InText, nullptr, false );
        if( root.is_discarded() || !root.is_object() )
        {
            return false;
        }
        *this = RenderTextureSettings();
        Reflection::FromJson( StaticType(), this, root );
        Clamp();
        return true;
    }


    std::string RenderTextureSettings::Write() const
    {
        json root;
        Reflection::ToJson( StaticType(), this, root );
        return root.dump( 4 );
    }


    bool RenderTextureSettings::IsRenderTexturePath( const std::string& InPath )
    {
        const size_t dot = InPath.find_last_of( '.' );
        if( dot == std::string::npos || InPath.find_first_of( "/\\", dot ) != std::string::npos )
        {
            return false;
        }
        std::string extension = InPath.substr( dot + 1 );
        std::transform( extension.begin(), extension.end(), extension.begin(), []( unsigned char c ) { return static_cast<char>( std::tolower( c ) ); } );
        return extension == kExtension;
    }
}
