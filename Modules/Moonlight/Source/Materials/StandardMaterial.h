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

    void OnSerialize( json& OutJson ) override;
    void OnDeserialize( const json& InJson ) override;

#if USING( ME_TOOLS )
    void OnEditorInspect() override;
#endif

    float Opacity = 1.f;
    float Metallic = 0.f;
    float Roughness = 0.5f;
    float NormalStrength = 1.f;
    float OcclusionStrength = 1.f;
    Vector3 EmissiveColor = Vector3( 0.f, 0.f, 0.f );
    float EmissiveIntensity = 1.f;
    float AlphaCutoff = 0.f;

protected:
    // Subclasses that keep their own registry name (e.g. the legacy DiffuseMaterial).
    StandardMaterial( const std::string& InTypeName );

private:
    inline static bgfx::UniformHandle u_baseColor = BGFX_INVALID_HANDLE;
    inline static bgfx::UniformHandle u_pbrParams = BGFX_INVALID_HANDLE;
    inline static bgfx::UniformHandle u_emissive = BGFX_INVALID_HANDLE;
    inline static bgfx::UniformHandle u_uvTransform = BGFX_INVALID_HANDLE;
};

ME_REGISTER_MATERIAL_NAME( StandardMaterial, "Standard (PBR)" )
