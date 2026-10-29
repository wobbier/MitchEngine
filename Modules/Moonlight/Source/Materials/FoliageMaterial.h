#pragma once
#include "Materials/StandardMaterial.h"

// The foliage part of a FoliageMaterial (plain data: JSON and the instancing key need no GPU).
struct FoliageSettings
{
    enum class Part : uint8_t
    {
        Bark = 0,
        Leaves,
    };

    Part Mode = Part::Bark;
    float Translucency = 0.f;       // leaves: light through them when back-lit
    float Variation = 0.f;          // per-branch tint variation
    // Wind response, filled in from the tree's recipe.
    float TreeHeight = 10.f;        // metres (the trunk lean grows with height up to this)
    float TrunkLean = 0.015f;       // top of the tree, as a fraction of its height, per unit of wind
    float BranchSway = 0.3f;        // metres at the most flexible twig, per unit of wind
    float LeafFlutter = 0.03f;      // metres at a leaf's tip
    float WindFrequency = 1.f;

    void Write( json& OutJson ) const;
    // Missing or invalid values keep their current value.
    void Read( const json& InJson );
    uint64_t Hash( uint64_t InSeed ) const;
};

// Bark and leaves of generated trees (MitchTree): Standard (PBR) plus hierarchical wind from the
// mesh's wind stream (MeshData::InitWindStream) and the scene's WindZone, the generator's baked
// crown occlusion and per-branch tint variation. Leaves add light shining through when back-lit
// (alpha testing and two-sided faces come from AlphaCutoff and FaceMode). Shadows sway with the
// tree. Meshes without a wind stream don't suit it.
class FoliageMaterial final
    : public StandardMaterial
{
public:
    FoliageMaterial();

    void Init() override;
    void Use() override;
    SharedPtr<Material> CreateInstance() override;
    uint64_t GetInstanceBatchKey() const override;
    bgfx::ProgramHandle GetSkinnedProgram() override
    {
        return BGFX_INVALID_HANDLE;
    }
    bgfx::ProgramHandle GetShadowProgram( bool InSkinned ) override;
    void UseShadowCaster() override;

    void OnSerialize( json& OutJson ) override;
    void OnDeserialize( const json& InJson ) override;

#if USING( ME_TOOLS )
    void OnEditorInspect() override;
#endif

    FoliageSettings Foliage;

private:
    void SetFoliageUniforms();

    // FoliageShadow.vert + .frag: the same wind on the shadow caster (requested in Init, on the main
    // thread, because render jobs ask for it).
    Moonlight::ShaderCommand m_shadowShader;
    bool m_shadowShaderRequested = false;

    inline static bgfx::UniformHandle u_foliage = BGFX_INVALID_HANDLE;
    inline static bgfx::UniformHandle u_foliageWind = BGFX_INVALID_HANDLE;
    inline static bgfx::UniformHandle u_shadowAlpha = BGFX_INVALID_HANDLE;
    inline static bgfx::UniformHandle u_shadowUvOffset = BGFX_INVALID_HANDLE;
    inline static bgfx::UniformHandle s_texDiffuse = BGFX_INVALID_HANDLE;
};

ME_REGISTER_MATERIAL_NAME( FoliageMaterial, "Foliage (MitchTree)" )
