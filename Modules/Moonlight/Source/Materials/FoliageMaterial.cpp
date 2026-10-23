#include "FoliageMaterial.h"
#include "Utils/HavanaUtils.h"
#include <algorithm>
#include <cmath>
#include <cstring>

#if USING( ME_TOOLS )
#include <imgui.h>
#endif

namespace
{
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
        const float value = ( it != InJson.end() && it->is_number() ) ? it->get<float>() : InFallback;
        return std::isfinite( value ) ? value : InFallback;
    }
}


void FoliageSettings::Write( json& OutJson ) const
{
    OutJson["Part"] = Mode == Part::Leaves ? "Leaves" : "Bark";
    OutJson["Translucency"] = Translucency;
    OutJson["Variation"] = Variation;
    OutJson["TreeHeight"] = TreeHeight;
    OutJson["TrunkLean"] = TrunkLean;
    OutJson["BranchSway"] = BranchSway;
    OutJson["LeafFlutter"] = LeafFlutter;
    OutJson["WindFrequency"] = WindFrequency;
}


void FoliageSettings::Read( const json& InJson )
{
    auto part = InJson.find( "Part" );
    if( part != InJson.end() && part->is_string() )
    {
        Mode = part->get<std::string>() == "Leaves" ? Part::Leaves : Part::Bark;
    }
    Translucency = ReadFloat( InJson, "Translucency", Translucency );
    Variation = ReadFloat( InJson, "Variation", Variation );
    TreeHeight = std::max( 0.1f, ReadFloat( InJson, "TreeHeight", TreeHeight ) );
    TrunkLean = ReadFloat( InJson, "TrunkLean", TrunkLean );
    BranchSway = ReadFloat( InJson, "BranchSway", BranchSway );
    LeafFlutter = ReadFloat( InJson, "LeafFlutter", LeafFlutter );
    WindFrequency = ReadFloat( InJson, "WindFrequency", WindFrequency );
}


uint64_t FoliageSettings::Hash( uint64_t InSeed ) const
{
    uint64_t h = Mix( InSeed, static_cast<uint64_t>( Mode ) );
    for( float value : { Translucency, Variation, TreeHeight, TrunkLean, BranchSway, LeafFlutter, WindFrequency } )
    {
        h = Mix( h, FloatBits( value ) );
    }
    return h;
}


FoliageMaterial::FoliageMaterial()
    : StandardMaterial( "FoliageMaterial", "Assets/Shaders/Foliage" )
{
}


void FoliageMaterial::Init()
{
    StandardMaterial::Init();
    if( !bgfx::isValid( u_foliage ) )
    {
        u_foliage = bgfx::createUniform( "u_foliage", bgfx::UniformType::Vec4 );
        u_foliageWind = bgfx::createUniform( "u_foliageWind", bgfx::UniformType::Vec4 );
        // Shared with the renderer's stock shadow caster (same names, same handles).
        u_shadowAlpha = bgfx::createUniform( "u_shadowAlpha", bgfx::UniformType::Vec4 );
        s_texDiffuse = bgfx::createUniform( "s_texDiffuse", bgfx::UniformType::Sampler );
    }
    if( !m_shadowShaderRequested )
    {
        m_shadowShaderRequested = true;
        m_shadowShader = Moonlight::ShaderCommand( "Assets/Shaders/FoliageShadow", Moonlight::ShaderLoad::Async );
    }
}


void FoliageMaterial::SetFoliageUniforms()
{
    const bool leaves = Foliage.Mode == FoliageSettings::Part::Leaves;
    const float foliage[4] = { Foliage.TreeHeight, leaves ? Foliage.Translucency : 0.f, Foliage.Variation, leaves ? 1.f : 0.f };
    const float wind[4] = { Foliage.TrunkLean, Foliage.BranchSway, leaves ? Foliage.LeafFlutter : 0.f, Foliage.WindFrequency };
    bgfx::setUniform( u_foliage, foliage );
    bgfx::setUniform( u_foliageWind, wind );
}


void FoliageMaterial::Use()
{
    StandardMaterial::Use();
    SetFoliageUniforms();
}


void FoliageMaterial::UseShadowCaster()
{
    SetFoliageUniforms();
    // Leaves cut their holes with the diffuse alpha; until the texture arrives they cast solid.
    const Moonlight::Texture* diffuse = GetTexture( Moonlight::TextureType::Diffuse );
    const bool textured = diffuse && bgfx::isValid( diffuse->TexHandle );
    const float shadowAlpha[4] = { textured ? AlphaCutoff : 0.f, Tiling.x, Tiling.y, 0.f };
    bgfx::setUniform( u_shadowAlpha, shadowAlpha );
    if( textured )
    {
        bgfx::setTexture( 0, s_texDiffuse, diffuse->TexHandle );
    }
}


SharedPtr<Moonlight::Material> FoliageMaterial::CreateInstance()
{
    return MakeShared<FoliageMaterial>( *this );
}


uint64_t FoliageMaterial::GetInstanceBatchKey() const
{
    return Foliage.Hash( StandardMaterial::GetInstanceBatchKey() );
}


bgfx::ProgramHandle FoliageMaterial::GetShadowProgram( bool InSkinned )
{
    return InSkinned ? bgfx::ProgramHandle( BGFX_INVALID_HANDLE ) : m_shadowShader.GetProgram();
}


void FoliageMaterial::OnSerialize( json& OutJson )
{
    StandardMaterial::OnSerialize( OutJson );
    Foliage.Write( OutJson );
}


void FoliageMaterial::OnDeserialize( const json& InJson )
{
    StandardMaterial::OnDeserialize( InJson );
    Foliage.Read( InJson );
}


#if USING( ME_TOOLS )
void FoliageMaterial::OnEditorInspect()
{
    StandardMaterial::OnEditorInspect();
    HavanaUtils::Label( "Part" );
    int mode = static_cast<int>( Foliage.Mode );
    if( ImGui::Combo( "##FoliagePart", &mode, "Bark\0Leaves\0" ) )
    {
        Foliage.Mode = static_cast<FoliageSettings::Part>( mode );
    }
    const bool leaves = Foliage.Mode == FoliageSettings::Part::Leaves;
    if( leaves )
    {
        HavanaUtils::Label( "Translucency" );
        ImGui::SliderFloat( "##Translucency", &Foliage.Translucency, 0.f, 2.f );
    }
    HavanaUtils::Label( "Tint Variation" );
    ImGui::SliderFloat( "##Variation", &Foliage.Variation, 0.f, 1.f );
    HavanaUtils::Label( "Tree Height" );
    ImGui::DragFloat( "##TreeHeight", &Foliage.TreeHeight, 0.05f, 0.1f, 200.f );
    HavanaUtils::Label( "Trunk Lean" );
    ImGui::SliderFloat( "##TrunkLean", &Foliage.TrunkLean, 0.f, 0.1f );
    HavanaUtils::Label( "Branch Sway (m)" );
    ImGui::SliderFloat( "##BranchSway", &Foliage.BranchSway, 0.f, 2.f );
    if( leaves )
    {
        HavanaUtils::Label( "Leaf Flutter (m)" );
        ImGui::SliderFloat( "##LeafFlutter", &Foliage.LeafFlutter, 0.f, 0.2f );
    }
    HavanaUtils::Label( "Wind Frequency" );
    ImGui::SliderFloat( "##WindFrequency", &Foliage.WindFrequency, 0.1f, 4.f );
}
#endif
