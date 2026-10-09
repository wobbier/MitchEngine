#include "StandardMaterial.h"
#include "Utils/HavanaUtils.h"
#include <cmath>
#include <cstring>

#if USING( ME_TOOLS )
#include <imgui.h>
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
    : Moonlight::Material( InTypeName, "Assets/Shaders/Standard" )
{
    SupportsInstancing = true;
}


void StandardMaterial::Init()
{
    if( !bgfx::isValid( u_baseColor ) )
    {
        u_baseColor = bgfx::createUniform( "u_baseColor", bgfx::UniformType::Vec4 );
        u_pbrParams = bgfx::createUniform( "u_pbrParams", bgfx::UniformType::Vec4 );
        u_emissive = bgfx::createUniform( "u_emissive", bgfx::UniformType::Vec4 );
        u_uvTransform = bgfx::createUniform( "u_uvTransform", bgfx::UniformType::Vec4 );
    }
}


void StandardMaterial::Use()
{
    // Colours are authored in sRGB; shading is linear.
    const float baseColor[4] = { SrgbToLinear( DiffuseColor.x ), SrgbToLinear( DiffuseColor.y ), SrgbToLinear( DiffuseColor.z ), Opacity };
    const float pbr[4] = { Metallic, Roughness, NormalStrength, OcclusionStrength };
    const float emissive[4] = { SrgbToLinear( EmissiveColor.x ) * EmissiveIntensity, SrgbToLinear( EmissiveColor.y ) * EmissiveIntensity, SrgbToLinear( EmissiveColor.z ) * EmissiveIntensity, AlphaCutoff };
    const float uvTransform[4] = { Tiling.x, Tiling.y, 0.f, 0.f };
    bgfx::setUniform( u_baseColor, baseColor );
    bgfx::setUniform( u_pbrParams, pbr );
    bgfx::setUniform( u_emissive, emissive );
    bgfx::setUniform( u_uvTransform, uvTransform );
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
}


void StandardMaterial::OnDeserialize( const json& InJson )
{
    Material::OnDeserialize( InJson );
    Opacity = ReadFloat( InJson, "Opacity", Opacity );
    Metallic = ReadFloat( InJson, "Metallic", Metallic );
    Roughness = ReadFloat( InJson, "Roughness", Roughness );
    NormalStrength = ReadFloat( InJson, "NormalStrength", NormalStrength );
    OcclusionStrength = ReadFloat( InJson, "OcclusionStrength", OcclusionStrength );
    EmissiveIntensity = ReadFloat( InJson, "EmissiveIntensity", EmissiveIntensity );
    AlphaCutoff = ReadFloat( InJson, "AlphaCutoff", AlphaCutoff );
    auto emissive = InJson.find( "EmissiveColor" );
    if( emissive != InJson.end() && emissive->is_array() && emissive->size() == 3 )
    {
        EmissiveColor = Vector3( ( *emissive )[0].get<float>(), ( *emissive )[1].get<float>(), ( *emissive )[2].get<float>() );
    }
}


#if USING( ME_TOOLS )
void StandardMaterial::OnEditorInspect()
{
    Material::OnEditorInspect();
    HavanaUtils::Label( "Opacity" );
    ImGui::SliderFloat( "##Opacity", &Opacity, 0.f, 1.f );
    HavanaUtils::Label( "Metallic" );
    ImGui::SliderFloat( "##Metallic", &Metallic, 0.f, 1.f );
    HavanaUtils::Label( "Roughness" );
    ImGui::SliderFloat( "##Roughness", &Roughness, 0.f, 1.f );
    HavanaUtils::Label( "Normal Strength" );
    ImGui::SliderFloat( "##NormalStrength", &NormalStrength, 0.f, 2.f );
    HavanaUtils::Label( "Occlusion Strength" );
    ImGui::SliderFloat( "##OcclusionStrength", &OcclusionStrength, 0.f, 1.f );
    HavanaUtils::Label( "Emissive" );
    ImGui::ColorEdit3( "##Emissive", &EmissiveColor.x );
    HavanaUtils::Label( "Emissive Intensity" );
    ImGui::DragFloat( "##EmissiveIntensity", &EmissiveIntensity, 0.05f, 0.f, 100.f );
    HavanaUtils::Label( "Alpha Cutoff" );
    ImGui::SliderFloat( "##AlphaCutoff", &AlphaCutoff, 0.f, 1.f );
}
#endif
