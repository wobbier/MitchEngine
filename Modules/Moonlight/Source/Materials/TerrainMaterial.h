#pragma once
#include "Materials/StandardMaterial.h"
#include <array>

// The first terrain surface path: four linear-blended PBR colors/roughnesses and one linear
// RGBA8 weight map. Lighting retains stages 8-15. Texture arrays are a later surface extension.
class TerrainMaterial final : public StandardMaterial
{
public:
    TerrainMaterial();
    void Init() override;
    void Use() override;
    SharedPtr<Material> CreateInstance() override;
    uint64_t GetInstanceBatchKey() const override;
    bool SetWeights( std::vector<std::array<uint8_t, 4>> InWeights, uint16_t InSide );
    bool LayersEnabled = false;
    std::array<std::array<float, 4>, 4> Layers{}; // sRGB color, roughness
private:
    std::shared_ptr<Moonlight::Texture> m_weights;
    std::vector<std::array<uint8_t, 4>> m_weightData;
    uint16_t m_side = 0;
    inline static bgfx::UniformHandle u_layers = BGFX_INVALID_HANDLE;
    inline static bgfx::UniformHandle u_surface = BGFX_INVALID_HANDLE;
    inline static bgfx::UniformHandle s_weights = BGFX_INVALID_HANDLE;
};
