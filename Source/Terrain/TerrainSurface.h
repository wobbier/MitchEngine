#pragma once
#include "Terrain/Heightfield.h"
#include <array>
#include <memory>
#include <span>

namespace TerrainData
{
    using LayerWeights = std::array<uint8_t, 4>;
    struct SurfaceLayer
    {
        std::string Name;
        std::array<float, 3> Color;
        float Roughness = 0.9f;
        bool operator==( const SurfaceLayer& ) const = default;
    };

    // Canonical signed lattice coordinates; shared edges have exactly one weight value.
    // Missing samples use BaseWeights. Fill changes the base and releases all overrides.
    struct Surface
    {
        std::array<SurfaceLayer, 4> Layers{{
            { "Grass", { 0.3f, 0.48f, 0.2f }, 0.9f }, { "Earth", { 0.42f, 0.28f, 0.16f }, 0.95f },
            { "Rock", { 0.48f, 0.49f, 0.5f }, 0.8f }, { "Sand", { 0.8f, 0.7f, 0.45f }, 0.9f } }};
        LayerWeights BaseWeights{ 255, 0, 0, 0 };
        std::map<std::pair<int64_t, int64_t>, LayerWeights> Samples;
        LayerWeights Get( SampleCoord InSample ) const;
        void Set( SampleCoord InSample, LayerWeights InWeights );
        bool Validate( const Settings& InSettings, std::string& OutError ) const;
        bool operator==( const Surface& ) const = default;
    };

    // Largest-remainder quantization guarantees a sum of 255; zero sum selects layer zero.
    LayerWeights NormalizeWeights( const std::array<float, 4>& InWeights );
    LayerWeights BlendLayer( LayerWeights InWeights, unsigned InLayer, float InAmount, bool InErase );
    // Full-resolution, linear RGBA8 samples with centers mapped to patch UVs, independent of LOD.
    std::vector<LayerWeights> BuildSurfacePatch( const Surface& InSurface, SampleCoord InOrigin, uint32_t InQuads );
    // Checksummed, little endian surface companion. Output arguments survive decoding errors.
    bool SerializeSurface( const Surface& InSurface, const Settings& InSettings, std::vector<uint8_t>& OutBytes, std::string& OutError );
    bool DeserializeSurface( std::span<const uint8_t> InBytes, const Settings& InSettings, Surface& OutSurface, std::string& OutError );
}
