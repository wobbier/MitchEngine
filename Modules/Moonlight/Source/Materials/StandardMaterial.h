#pragma once
#include "Graphics/Material.h"

// Metallic-roughness PBR material (glTF conventions). The base colour is Material::DiffuseColor and
// the Diffuse texture slot; MetallicRoughness (G rough, B metal), Occlusion, Emissive, Normal and
// Opacity slots are optional. AlphaCutoff > 0 enables alpha testing.
class StandardMaterial
    : public Moonlight::Material
{
public:
    StandardMaterial();

    void Init() override;
    void Use() override;
    SharedPtr<Material> CreateInstance() override;
    uint64_t GetInstanceBatchKey() const override;
    float GetAlphaCutoff() const override { return AlphaCutoff; }
    bgfx::ProgramHandle GetSkinnedProgram() override;

    void OnSerialize( json& OutJson ) override;
    void OnDeserialize( const json& InJson ) override;

#if USING( ME_TOOLS )
    void OnEditorInspect() override;
#endif

    // All of these are in the instancing batch key: write them through the setters, or call
    // MarkChanged() after writing a field (ME-399).
    void SetOpacity( float InValue );
    void SetMetallic( float InValue );
    void SetRoughness( float InValue );
    void SetNormalStrength( float InValue );
    void SetOcclusionStrength( float InValue );
    void SetEmissiveColor( const Vector3& InValue );
    void SetEmissiveIntensity( float InValue );
    void SetAlphaCutoff( float InValue );
    void SetInvertRoughness( bool InValue );

    ME_MATERIAL_FIELD float Opacity = 1.f;
    ME_MATERIAL_FIELD float Metallic = 0.f;
    ME_MATERIAL_FIELD float Roughness = 0.5f;
    ME_MATERIAL_FIELD float NormalStrength = 1.f;
    ME_MATERIAL_FIELD float OcclusionStrength = 1.f;
    ME_MATERIAL_FIELD Vector3 EmissiveColor = Vector3( 0.f, 0.f, 0.f );
    ME_MATERIAL_FIELD float EmissiveIntensity = 1.f;
    ME_MATERIAL_FIELD float AlphaCutoff = 0.f;
    ME_MATERIAL_FIELD bool InvertRoughness = false;

protected:
    // Subclasses that keep their own registry name (e.g. the legacy DiffuseMaterial), or also draw
    // with their own program (FoliageMaterial).
    StandardMaterial( const std::string& InTypeName );
    StandardMaterial( const std::string& InTypeName, const std::string& InShaderPath );

private:
    // StandardSkinned.vert + Standard.frag, created on first use (shared through the program registry).
    Moonlight::ShaderCommand m_skinnedShader;
    bool m_skinnedShaderRequested = false;

    inline static bgfx::UniformHandle u_baseColor = BGFX_INVALID_HANDLE;
    inline static bgfx::UniformHandle u_pbrParams = BGFX_INVALID_HANDLE;
    inline static bgfx::UniformHandle u_emissive = BGFX_INVALID_HANDLE;
    inline static bgfx::UniformHandle u_uvTransform = BGFX_INVALID_HANDLE;
    inline static bgfx::UniformHandle u_pbrTextureParams = BGFX_INVALID_HANDLE;
};

ME_REGISTER_MATERIAL_NAME( StandardMaterial, "Standard (PBR)" )
