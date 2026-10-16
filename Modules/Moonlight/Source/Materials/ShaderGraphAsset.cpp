#include "ShaderGraphAsset.h"
#include "CLog.h"
#include "File.h"
#include "Resource/ResourceCache.h"
#include "Utils/BGFXUtils.h"
#include <cmath>

namespace Moonlight
{
    namespace
    {
        float SrgbToLinear( float InValue )
        {
            return InValue <= 0.04045f ? InValue / 12.92f : std::pow( ( InValue + 0.055f ) / 1.055f, 2.4f );
        }


        Vector4 ReadVector( const json& InJson, const Vector4& InFallback )
        {
            Vector4 result = InFallback;
            if( !InJson.is_array() )
            {
                return result;
            }
            float* components[4] = { &result.x, &result.y, &result.z, &result.w };
            for( size_t i = 0; i < InJson.size() && i < 4; ++i )
            {
                if( InJson[i].is_number() )
                {
                    *components[i] = InJson[i].get<float>();
                }
            }
            return result;
        }


        int ComponentsOf( const std::string& InType )
        {
            if( InType == "Vec2" )
            {
                return 2;
            }
            if( InType == "Vec3" )
            {
                return 3;
            }
            if( InType == "Vec4" )
            {
                return 4;
            }
            return 1;
        }
    }


    ShaderGraphAsset::ShaderGraphAsset( const Path& InManifestPath )
        : Resource( InManifestPath )
    {
    }


    Path ShaderGraphAsset::ManifestPathFor( const std::string& InGraphPath )
    {
        const std::string manifest = "/Material.json";
        if( InGraphPath.size() > manifest.size() && InGraphPath.compare( InGraphPath.size() - manifest.size(), manifest.size(), manifest ) == 0 )
        {
            return Path( InGraphPath );
        }
        const std::string extension = ".shadergraph";
        std::string stem = InGraphPath;
        if( stem.size() > extension.size() && stem.compare( stem.size() - extension.size(), extension.size(), extension ) == 0 )
        {
            stem.resize( stem.size() - extension.size() );
        }
        return Path( stem + ".sg" + manifest );
    }


    void ShaderGraphAsset::CreateUniforms()
    {
        if( bgfx::isValid( s_params ) || !IsGpuAlive() )
        {
            return;
        }
        s_params = bgfx::createUniform( "u_sgParams", bgfx::UniformType::Vec4, kMaxParameters );
        for( int i = 0; i < kMaxTextures; ++i )
        {
            s_samplers[i] = bgfx::createUniform( ( "s_sgTex" + std::to_string( i ) ).c_str(), bgfx::UniformType::Sampler );
        }
    }


    bool ShaderGraphAsset::Load()
    {
        CreateUniforms();
        Parse();
        // A graph that hasn't been compiled yet still "loads": materials keep their values and
        // pick the programs up when the editor writes them.
        return true;
    }


    void ShaderGraphAsset::Reload()
    {
        Parse();
    }


    bool ShaderGraphAsset::Parse()
    {
        ++m_revision;
        m_valid = false;
        File file( FilePath );
        if( !FilePath.Exists )
        {
            return false;
        }
        json manifest = json::parse( file.Read(), nullptr, false );
        if( manifest.is_discarded() || !manifest.is_object() )
        {
            YIKES( "Invalid shader graph manifest: " + FilePath.GetLocalPathString() );
            return false;
        }
        m_manifest = manifest;
        m_graphPath = manifest.value( "Graph", std::string() );
        m_target = manifest.value( "Target", std::string( "Lit" ) );
        m_transparent = manifest.value( "Surface", std::string( "Opaque" ) ) == "Transparent";
        m_blend = manifest.value( "Blend", std::string( "Alpha" ) );
        m_renderFace = manifest.value( "RenderFace", std::string( "Front" ) );
        m_alphaClip = manifest.value( "AlphaClip", false );
        m_castShadows = manifest.value( "CastShadows", true );

        // Programs: sources sit next to the manifest. ShaderCommand shares programs by path, so
        // reloading keeps unchanged ones (and hot reload rebuilds changed ones in place).
        const std::string directory = FilePath.GetDirectoryString();
        std::map<std::string, ShaderCommand> programs;
        if( manifest.contains( "Programs" ) && manifest["Programs"].is_object() )
        {
            for( auto it = manifest["Programs"].begin(); it != manifest["Programs"].end(); ++it )
            {
                const json& pair = it.value();
                if( !pair.is_array() || pair.size() != 2 )
                {
                    continue;
                }
                const std::string vertex = Path( directory + pair[0].get<std::string>() ).GetLocalPathString();
                const std::string fragment = Path( directory + pair[1].get<std::string>() ).GetLocalPathString();
                programs[it.key()] = ShaderCommand( vertex, fragment, ShaderLoad::Async );
            }
        }
        m_programs = std::move( programs );

        m_parameters.clear();
        if( manifest.contains( "Parameters" ) )
        {
            for( const json& entry : manifest["Parameters"] )
            {
                Parameter parameter;
                parameter.Name = entry.value( "Name", std::string() );
                parameter.Id = entry.value( "Id", std::string() );
                parameter.Type = entry.value( "Type", std::string( "Float" ) );
                parameter.Slot = entry.value( "Slot", 0 );
                parameter.Components = ComponentsOf( parameter.Type );
                parameter.Default = ReadVector( entry.value( "Default", json() ), Vector4() );
                parameter.IsColor = entry.value( "Color", false );
                parameter.IsHDR = entry.value( "HDR", false );
                parameter.Exposed = entry.value( "Exposed", true );
                if( entry.contains( "Range" ) && entry["Range"].is_array() && entry["Range"].size() == 2 )
                {
                    parameter.Min = entry["Range"][0].get<float>();
                    parameter.Max = entry["Range"][1].get<float>();
                }
                if( parameter.Slot >= 0 && parameter.Slot < kMaxParameters )
                {
                    m_parameters.push_back( parameter );
                }
            }
        }

        m_textures.clear();
        if( manifest.contains( "Textures" ) )
        {
            for( const json& entry : manifest["Textures"] )
            {
                TextureSlot texture;
                texture.Name = entry.value( "Name", std::string() );
                texture.Id = entry.value( "Id", std::string() );
                texture.Slot = entry.value( "Slot", 0 );
                texture.IsCube = entry.value( "Type", std::string() ) == "TextureCube";
                texture.Exposed = entry.value( "Exposed", false );
                texture.Asset = entry.value( "Asset", std::string() );
                texture.Fallback = texture.IsCube ? DefaultTexture::BlackCube : DefaultTextureFromName( entry.value( "Default", std::string( "white" ) ) );
                if( !texture.Asset.empty() )
                {
                    texture.Loaded = ResourceCache::GetInstance().GetAsync<Texture>( Path( texture.Asset ) );
                }
                if( texture.Slot >= 0 && texture.Slot < kMaxTextures )
                {
                    m_textures.push_back( texture );
                }
            }
        }
        m_valid = true;
        return true;
    }


    const ShaderCommand* ShaderGraphAsset::GetProgram( const std::string& InName ) const
    {
        auto found = m_programs.find( InName );
        return found == m_programs.end() ? nullptr : &found->second;
    }


    bgfx::ProgramHandle ShaderGraphAsset::GetProgramHandle( const std::string& InName ) const
    {
        const ShaderCommand* program = GetProgram( InName );
        return program ? program->GetProgram() : bgfx::ProgramHandle( BGFX_INVALID_HANDLE );
    }


    const ShaderGraphAsset::Parameter* ShaderGraphAsset::FindParameter( const std::string& InName ) const
    {
        for( const Parameter& parameter : m_parameters )
        {
            if( parameter.Name == InName )
            {
                return &parameter;
            }
        }
        return nullptr;
    }


    const ShaderGraphAsset::TextureSlot* ShaderGraphAsset::FindTexture( const std::string& InName ) const
    {
        for( const TextureSlot& texture : m_textures )
        {
            if( texture.Name == InName )
            {
                return &texture;
            }
        }
        return nullptr;
    }


    void ShaderGraphAsset::PackParameters( const std::map<std::string, Vector4>& InValues, Vector4 OutBlock[kMaxParameters] ) const
    {
        for( int i = 0; i < kMaxParameters; ++i )
        {
            OutBlock[i] = Vector4();
        }
        for( const Parameter& parameter : m_parameters )
        {
            auto found = InValues.find( parameter.Name );
            Vector4 value = found != InValues.end() ? found->second : parameter.Default;
            if( parameter.IsColor )
            {
                value = Vector4( SrgbToLinear( value.x ), SrgbToLinear( value.y ), SrgbToLinear( value.z ), value.w );
            }
            OutBlock[parameter.Slot] = value;
        }
    }


    void ShaderGraphAsset::Bind( const Vector4 InBlock[kMaxParameters], const std::map<std::string, SharedPtr<Texture>>& InTextures ) const
    {
        if( !bgfx::isValid( s_params ) )
        {
            return;
        }
        static_assert( sizeof( Vector4 ) == sizeof( float ) * 4, "Vector4 must pack as four floats" );
        bgfx::setUniform( s_params, InBlock, kMaxParameters );
        for( const TextureSlot& slot : m_textures )
        {
            bgfx::TextureHandle handle = BGFX_INVALID_HANDLE;
            auto overridden = InTextures.find( slot.Name );
            if( overridden != InTextures.end() && overridden->second && bgfx::isValid( overridden->second->TexHandle ) )
            {
                handle = overridden->second->TexHandle;
            }
            else if( slot.Loaded && bgfx::isValid( slot.Loaded->TexHandle ) )
            {
                handle = slot.Loaded->TexHandle;
            }
            if( !bgfx::isValid( handle ) )
            {
                handle = GetDefaultTexture( slot.Fallback );
            }
            if( bgfx::isValid( handle ) )
            {
                bgfx::setTexture( static_cast<uint8_t>( slot.Slot ), s_samplers[slot.Slot], handle );
            }
        }
    }
}
