#include "Material.h"
#include "optick.h"
#include "Texture.h"
#include "Resource/ResourceCache.h"
#include "bgfx/defines.h"

namespace Moonlight
{
    Material::Material( const std::string& MaterialTypeName, const std::string& ShaderPath )
        : Textures( TextureType::Count, nullptr )
        , DiffuseColor( 1.f, 1.f, 1.f )
        , Tiling( 1.f, 1.f )
        , TypeName( MaterialTypeName )
    {
        LoadShader( ShaderPath );
    }

    Material::~Material()
    {
    }

    void Material::LoadShader( const std::string& inShaderName )
    {
        ShaderName = inShaderName;
        if( inShaderName.length() > 0 )
        {
            MeshShader = ShaderCommand( inShaderName );
        }
    }

    const bool Material::IsTransparent() const
    {
        if( RenderMode == RenderingMode::Transparent )
        {
            return true;
        }
        else if( Textures[TextureType::Opacity] )
        {
            return true;
        }
        return false;
    }

    void Material::SetRenderMode( RenderingMode newMode )
    {
        RenderMode = newMode;
    }

    void Material::OnSerialize( json& OutJson )
    {
        OutJson["Type"] = TypeName;
        OutJson["FaceMode"] = FaceMode;
        OutJson["BlendMode"] = AlphaBlendMode;
        OutJson["RenderMode"] = RenderMode;
        OutJson["DiffuseColor"] = { DiffuseColor.x, DiffuseColor.y, DiffuseColor.z };
        OutJson["Tiling"] = { Tiling.x, Tiling.y };
        for( unsigned int type = 0; type < TextureType::Count; ++type )
        {
            auto texture = Textures[type];
            if( texture )
            {
                json& savedTexture = OutJson["Textures"][Texture::ToString( static_cast<TextureType>( type ) )];
                savedTexture["Path"] = texture->GetPath().GetLocalPath();
                savedTexture["RenderMode"] = GetRenderingModeString( RenderMode );
            }
        }
    }

    void Material::OnDeserialize( const json& InJson )
    {
        if( InJson.contains( "Type" ) )
        {
            TypeName = InJson["Type"];
        }
        if( InJson.contains( "DiffuseColor" ) )
        {
            DiffuseColor = Vector3( (float)InJson["DiffuseColor"][0], (float)InJson["DiffuseColor"][1], (float)InJson["DiffuseColor"][2] );
        }
        if( InJson.contains( "Tiling" ) )
        {
            Tiling = Vector2( (float)InJson["Tiling"][0], (float)InJson["Tiling"][1] );
        }
        if( InJson.contains( "FaceMode" ) )
        {
            FaceMode = (RenderFaceMode)InJson["FaceMode"];
        }
        if( InJson.contains( "BlendMode" ) )
        {
            AlphaBlendMode = (BlendMode)InJson["BlendMode"];
        }
        if( InJson.contains( "RenderMode" ) )
        {
            RenderMode = (RenderingMode)InJson["RenderMode"];
        }
        if( InJson.contains( "Textures" ) )
        {
            for( unsigned int type = 0; type < TextureType::Count; ++type )
            {
                if( InJson["Textures"].contains( Texture::ToString( static_cast<TextureType>( type ) ) ) )
                {
                    Path texturePath = Path( InJson["Textures"][Texture::ToString( static_cast<TextureType>( type ) )]["Path"] );
                    SetTexture( static_cast<TextureType>( type ), ResourceCache::GetInstance().Get<Moonlight::Texture>( texturePath ) );
                }
            }
        }
    }

    void Material::CopyValues( Material* mat )
    {
        DiffuseColor = mat->DiffuseColor;
        Textures = mat->Textures;
        RenderMode = mat->RenderMode;
        Tiling = mat->Tiling;
        MeshShader = mat->MeshShader;
        AlphaBlendMode = mat->AlphaBlendMode;
    }

    void Material::SetTexture( const TextureType& textureType, std::shared_ptr<Moonlight::Texture> loadedTexture )
    {
        Textures[textureType] = loadedTexture;
    }

    const Moonlight::Texture* Material::GetTexture( const TextureType& type ) const
    {
        if( Textures[type] )
        {
            return Textures[type].get();
        }
        return nullptr;
    }

    std::vector<std::shared_ptr<Moonlight::Texture>>& Material::GetTextures()
    {
        return Textures;
    }

    const std::string& Material::GetTypeName() const
    {
        return TypeName;
    }

    uint64_t Material::GetRenderState( uint64_t state ) const
    {
        uint64_t modifiedState = state;
        // this all should be applied when changed and not polled

        if( IsTransparent() )
        {
            switch( AlphaBlendMode )
            {
            case Moonlight::BlendMode::Alpha:
                modifiedState = modifiedState | BGFX_STATE_BLEND_ALPHA;
                break;
            case Moonlight::BlendMode::Premultiply:
                modifiedState = modifiedState | BGFX_STATE_BLEND_FUNC( BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_INV_SRC_ALPHA );
                break;
            case Moonlight::BlendMode::Additive:
                modifiedState = modifiedState | BGFX_STATE_BLEND_ADD;
                break;
            case Moonlight::BlendMode::Multiply:
                modifiedState = modifiedState | BGFX_STATE_BLEND_MULTIPLY;
                break;
            case Moonlight::BlendMode::Count:
            default:
                modifiedState = modifiedState | BGFX_STATE_BLEND_ALPHA;
                break;
            }
        }

        switch( FaceMode )
        {
        case Moonlight::RenderFaceMode::Front:
            modifiedState = modifiedState | BGFX_STATE_CULL_CCW;
            break;
        case Moonlight::RenderFaceMode::Back:
            modifiedState = modifiedState | BGFX_STATE_CULL_CW;
            break;
        case Moonlight::RenderFaceMode::Both:
            // none?
            break;
        case Moonlight::RenderFaceMode::Count:
        default:
            break;
        }

        return modifiedState;
    }
}
