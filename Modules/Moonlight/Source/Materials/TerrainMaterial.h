#pragma once
#include "Materials/StandardMaterial.h"
#include <array>
#include "Graphics/TerrainTextureSet.h"

// Four PBR layers: linear RGBA8 weights and shared base-color/normal/ORM arrays or padded atlases.
// Material bindings occupy stages 0-3; lighting retains stages 8-15.
class TerrainMaterial final : public StandardMaterial
{
public:
    TerrainMaterial();
    void Init() override;
    void Use() override;
    SharedPtr<Material> CreateInstance() override;
    uint64_t GetInstanceBatchKey() const override;
    bool SetWeights( std::vector<std::array<uint8_t, 4>> InWeights, uint16_t InSide );
    const std::vector<std::array<uint8_t, 4>>& GetWeights() const { return m_weightData; }
    void SetLayerTextures( std::shared_ptr<Moonlight::TerrainTextureSet> InTextures );
    const std::shared_ptr<Moonlight::TerrainTextureSet>& GetLayerTextures() const { return m_layerTextures; }
    bool LayersEnabled = false;
    Vector2 TerrainTranslation;
    std::array<std::array<float, 4>, 4> Mapping{}; // inverse tile metres, normal strength, metallic, normal Y sign
    std::array<std::array<float, 4>, 4> Layers{}; // sRGB color, roughness
private:
    std::shared_ptr<Moonlight::TerrainTextureSet> m_layerTextures;
    Moonlight::ShaderCommand m_atlasShader, m_arrayShader;
    std::shared_ptr<Moonlight::Texture> m_weights;
    std::vector<std::array<uint8_t, 4>> m_weightData;
    uint16_t m_side = 0;
    inline static bgfx::UniformHandle u_layers = BGFX_INVALID_HANDLE;
    inline static bgfx::UniformHandle u_surface = BGFX_INVALID_HANDLE;
    inline static bgfx::UniformHandle u_mapping = BGFX_INVALID_HANDLE;
    inline static bgfx::UniformHandle u_textures = BGFX_INVALID_HANDLE;
    inline static bgfx::UniformHandle u_origin = BGFX_INVALID_HANDLE;
    inline static std::array<bgfx::UniformHandle, 3> s_maps{{ BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE }};
    inline static bgfx::UniformHandle s_weights = BGFX_INVALID_HANDLE;
};
