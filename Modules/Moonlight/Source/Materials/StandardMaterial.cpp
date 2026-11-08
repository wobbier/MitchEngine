#include "StandardMaterial.h"
#include "Utils/HavanaUtils.h"
#include <cmath>
#include <cstring>

#if USING( ME_TOOLS )
#include <imgui.h>
#include "Widgets/MaterialWidgets.h"
#endif

namespace
{
    float SrgbToLinear( float c )
    {
        return c <= 0.04045f ? c / 12.92f : std::pow( ( c + 0.055f ) / 1.055f, 2.4f );
    }

    uint64_t FloatBits( float f )
    {
        uint32_t bits = 0u;
        std::memcpy( &bits, &f, sizeof( bits ) );
        return bits;
    }

    uint64_t Mix( uint64_t h, uint64_t v )
    {
        return h ^ ( v + 0x9e3779b97f4a7c15ULL + ( h << 6 ) + ( h >> 2 ) );
    }

    float ReadFloat( const json& InJson, const char* InKey, float InFallback )
    {
        auto it = InJson.find( InKey );
        return ( it != InJson.end() && it->is_number() ) ? it->get<float>() : InFallback;
    }
}


StandardMaterial::StandardMaterial()
    : StandardMaterial( "StandardMaterial" )
{
}


StandardMaterial::StandardMaterial( const std::string& InTypeName )
    : StandardMaterial( InTypeName, "Assets/Shaders/Standard" )
{
}


StandardMaterial::StandardMaterial( const std::string& InTypeName, const std::string& InShaderPath )
    : Moonlight::Material( InTypeName, InShaderPath )
{
    SupportsInstancing = true;
}


bgfx::ProgramHandle StandardMaterial::GetSkinnedProgram()
{
    if( !m_skinnedShaderRequested )
    {
        m_skinnedShaderRequested = true;
        m_skinnedShader = Moonlight::ShaderCommand( "Assets/Shaders/StandardSkinned", "Assets/Shaders/Standard", Moonlight::ShaderLoad::Async );
    }
    return m_skinnedShader.GetProgram();
}


void StandardMaterial::Init()
{
    if( !bgfx::isValid( u_baseColor ) )
    {
        u_baseColor = bgfx::createUniform( "u_baseColor", bgfx::UniformType::Vec4 );
        u_pbrParams = bgfx::createUniform( "u_pbrParams", bgfx::UniformType::Vec4 );
        u_emissive = bgfx::createUniform( "u_emissive", bgfx::UniformType::Vec4 );
        u_uvTransform = bgfx::createUniform( "u_uvTransform", bgfx::UniformType::Vec4 );
        u_pbrTextureParams = bgfx::createUniform( "u_pbrTextureParams", bgfx::UniformType::Vec4 );
    }
}


void StandardMaterial::Use()
{
    // Colours are authored in sRGB; shading is linear.
    const float baseColor[4] = { SrgbToLinear( DiffuseColor.x ), SrgbToLinear( DiffuseColor.y ), SrgbToLinear( DiffuseColor.z ), Opacity };
    const float pbr[4] = { Metallic, Roughness, NormalStrength, OcclusionStrength };
    const float emissive[4] = { SrgbToLinear( EmissiveColor.x ) * EmissiveIntensity, SrgbToLinear( EmissiveColor.y ) * EmissiveIntensity, SrgbToLinear( EmissiveColor.z ) * EmissiveIntensity, AlphaCutoff };
    const float uvTransform[4] = { Tiling.x, Tiling.y, Offset.x, Offset.y };
    bgfx::setUniform( u_baseColor, baseColor );
    bgfx::setUniform( u_pbrParams, pbr );
    bgfx::setUniform( u_emissive, emissive );
    bgfx::setUniform( u_uvTransform, uvTransform );
    const float maps[4] = { GetTexture( Moonlight::TextureType::Metallic ) ? 1.f : 0.f, GetTexture( Moonlight::TextureType::Roughness ) ? 1.f : 0.f, InvertRoughness ? 1.f : 0.f, 0.f };
    bgfx::setUniform( u_pbrTextureParams, maps );
}


SharedPtr<Moonlight::Material> StandardMaterial::CreateInstance()
{
    return MakeShared<StandardMaterial>( *this );
}


uint64_t StandardMaterial::GetInstanceBatchKey() const
{
    uint64_t h = Material::GetInstanceBatchKey();
    auto texId = [this]( Moonlight::TextureType type ) -> uint64_t {
        const Moonlight::Texture* texture = GetTexture( type );
        return ( texture && bgfx::isValid( texture->TexHandle ) ) ? texture->TexHandle.idx : 0xFFFFu;
    };
    h = Mix( h, texId( Moonlight::TextureType::MetallicRoughness ) );
    h = Mix( h, texId( Moonlight::TextureType::Emissive ) );
    h = Mix( h, texId( Moonlight::TextureType::Occlusion ) );
    h = Mix( h, texId( Moonlight::TextureType::Metallic ) );
    h = Mix( h, texId( Moonlight::TextureType::Roughness ) );
    h = Mix( h, InvertRoughness );
    for( float value : { Opacity, Metallic, Roughness, NormalStrength, OcclusionStrength, EmissiveColor.x, EmissiveColor.y, EmissiveColor.z, EmissiveIntensity, AlphaCutoff } )
    {
        h = Mix( h, FloatBits( value ) );
    }
    return h;
}


void StandardMaterial::OnSerialize( json& OutJson )
{
    Material::OnSerialize( OutJson );
    OutJson["Opacity"] = Opacity;
    OutJson["Metallic"] = Metallic;
    OutJson["Roughness"] = Roughness;
    OutJson["NormalStrength"] = NormalStrength;
    OutJson["OcclusionStrength"] = OcclusionStrength;
    OutJson["EmissiveColor"] = { EmissiveColor.x, EmissiveColor.y, EmissiveColor.z };
    OutJson["EmissiveIntensity"] = EmissiveIntensity;
    OutJson["AlphaCutoff"] = AlphaCutoff;
    OutJson["InvertRoughness"] = InvertRoughness;
}


void StandardMaterial::OnDeserialize( const json& InJson )
{
    Material::OnDeserialize( InJson );
    Opacity = ReadFloat( InJson, "Opacity", Opacity );
    Metallic = ReadFloat( InJson, "Metallic", Metallic );
    const float roughness = ReadFloat( InJson, "Roughness", Roughness );
    Roughness = std::isfinite( roughness ) && roughness >= 0.f && roughness <= 1.f ? roughness : 0.5f;
    NormalStrength = ReadFloat( InJson, "NormalStrength", NormalStrength );
    OcclusionStrength = ReadFloat( InJson, "OcclusionStrength", OcclusionStrength );
    EmissiveIntensity = ReadFloat( InJson, "EmissiveIntensity", EmissiveIntensity );
    AlphaCutoff = ReadFloat( InJson, "AlphaCutoff", AlphaCutoff );
    InvertRoughness = InJson.value( "InvertRoughness", InvertRoughness );
    auto emissive = InJson.find( "EmissiveColor" );
    if( emissive != InJson.end() && emissive->is_array() && emissive->size() == 3 )
    {
        EmissiveColor = Vector3( ( *emissive )[0].get<float>(), ( *emissive )[1].get<float>(), ( *emissive )[2].get<float>() );
    }
}


#if USING( ME_TOOLS )
void StandardMaterial::OnEditorInspect()
{
    // Material edits live in the owning Mesh's JSON, so the inspector's per-interaction diff
    // makes each gesture here one undo step (UX-12).
    Material::OnEditorInspect();
    auto spec = []( MeWidgets::Unit InUnit, double InMax, double InDefault, int InDecimals ) {
        MeWidgets::NumberSpec result;
        result.FieldUnit = InUnit;
        result.HasRange = true;
        result.Min = 0.0;
        result.Max = InMax;
        result.HasDefault = true;
        result.Default = InDefault;
        result.Decimals = InDecimals;
        return result;
    };
    auto row = []( const char* InLabel, float& InOutValue, const MeWidgets::NumberSpec& InSpec ) {
        double value = InOutValue;
        if( MeWidgets::UnitRow( InLabel, value, InSpec ) )
        {
            InOutValue = static_cast<float>( value );
        }
    };
    const float baseColor[3] = { DiffuseColor.x, DiffuseColor.y, DiffuseColor.z };
    row( "Opacity", Opacity, spec( MeWidgets::Unit::Percent, 1.0, 1.0, -1 ) );

    double metallic = Metallic;
    double roughness = Roughness;
    if( MeWidgets::MetalRoughPad( "Surface", metallic, roughness, baseColor ) )
    {
        Metallic = static_cast<float>( metallic );
        Roughness = static_cast<float>( roughness );
    }
    row( "Metallic", Metallic, spec( MeWidgets::Unit::None, 1.0, 0.0, 2 ) );
    row( "Roughness", Roughness, spec( MeWidgets::Unit::None, 1.0, 0.5, 2 ) );

    const MeWidgets::NumberSpec normal = spec( MeWidgets::Unit::Multiplier, 2.0, 1.0, 2 );
    row( "Normal Strength", NormalStrength, normal );
    double normalStrength = NormalStrength;
    if( MeWidgets::BumpPreview( "Relief", normalStrength, normal, baseColor ) )
    {
        NormalStrength = static_cast<float>( normalStrength );
    }
    row( "Occlusion Strength", OcclusionStrength, spec( MeWidgets::Unit::Percent, 1.0, 1.0, -1 ) );

    HavanaUtils::Label( "Emissive" );
    ImGui::ColorEdit3( "##Emissive", &EmissiveColor.x );
    double ev = MeWidgets::MaterialMath::IntensityToEV( EmissiveIntensity );
    if( MeWidgets::UnitRow( "Emissive Intensity", ev, MeWidgets::MaterialMath::EmissiveSpec() ) )
    {
        EmissiveIntensity = static_cast<float>( MeWidgets::MaterialMath::EVToIntensity( ev ) );
    }
    const float emissive[3] = { EmissiveColor.x, EmissiveColor.y, EmissiveColor.z };
    double intensity = EmissiveIntensity;
    if( MeWidgets::EmissiveStrip( "Glow", intensity, emissive ) )
    {
        EmissiveIntensity = static_cast<float>( intensity );
    }
    row( "Alpha Cutoff", AlphaCutoff, spec( MeWidgets::Unit::Percent, 1.0, 0.0, -1 ) );
}
#endif
