#pragma once
#include <cstdint>

// Deterministic procedural noise (terrain, trees, textures). All functions are pure, thread safe and
// seeded explicitly; results depend only on the inputs (see Math/Hash.h).
namespace Noise
{
    // A 2D sample with its analytic gradient (d/dx, d/dy).
    struct Sample2
    {
        float Value = 0.f;
        float Dx = 0.f;
        float Dy = 0.f;
    };

    // Gradient (Perlin-style) noise with a quintic fade, roughly in [-1, 1]; zero on lattice points.
    Sample2 Gradient2( float InX, float InY, uint32_t InSeed );
    float Value2( float InX, float InY, uint32_t InSeed );
    // Tiles with the given integer lattice periods (> 0), for seamless textures.
    Sample2 GradientPeriodic2( float InX, float InY, int32_t InPeriodX, int32_t InPeriodY, uint32_t InSeed );
    // 3D gradient noise, roughly in [-1, 1].
    float Value3( float InX, float InY, float InZ, uint32_t InSeed );

    struct Fractal
    {
        int Octaves = 6;
        float Lacunarity = 2.f;
        // Amplitude ratio between octaves (fbm/ridged).
        float Gain = 0.5f;
    };

    // Sum of octaves, normalized to roughly [-1, 1]. Octaves are rotated against each other so the
    // lattice never lines up. OutDx/OutDy (optional) receive the gradient in input space.
    float Fbm2( float InX, float InY, uint32_t InSeed, const Fractal& InFractal, float* OutDx = nullptr, float* OutDy = nullptr );
    // Periodic fbm: each octave tiles with InPeriod * Lacunarity^i (use integer lacunarity), no rotation.
    float FbmPeriodic2( float InX, float InY, int32_t InPeriodX, int32_t InPeriodY, uint32_t InSeed, const Fractal& InFractal );
    float Fbm3( float InX, float InY, float InZ, uint32_t InSeed, const Fractal& InFractal );

    // Musgrave's ridged multifractal, in roughly [0, 1]: sharp crests where noise crosses zero,
    // with each octave weighted by the previous one so detail concentrates on the ridges.
    float Ridged2( float InX, float InY, uint32_t InSeed, const Fractal& InFractal, float InOffset = 1.f, float InRidgeGain = 2.f );

    // Musgrave's hybrid multifractal: each octave is scaled by the altitude accumulated so far, so
    // low areas stay smooth while high areas get rough (real terrain statistics vary with height).
    // InH is the fractal increment (0 = rough, 1 = smooth); InOffset lifts the noise before weighting.
    // Returns roughly [0, 2] for the default offset; callers normalize to their own range.
    float HybridMultifractal2( float InX, float InY, uint32_t InSeed, int InOctaves, float InLacunarity, float InH, float InOffset );

    // Slope-damped fbm (eroded look): each octave's amplitude falls with the accumulated slope, so
    // steep flanks stay smooth and detail gathers in flat valleys and on peaks.
    float ErodedFbm2( float InX, float InY, uint32_t InSeed, const Fractal& InFractal, float InDamping = 1.f );

    struct Cell
    {
        // Distances to the nearest and second-nearest feature points.
        float F1 = 0.f;
        float F2 = 0.f;
        // The nearest feature point and its cell's hash.
        float X = 0.f;
        float Y = 0.f;
        uint32_t Id = 0;
    };

    // Worley/Voronoi cells. InJitter in [0, 1] (0 = a regular grid). Periods of zero mean unbounded;
    // positive periods tile the pattern.
    Cell Voronoi2( float InX, float InY, uint32_t InSeed, float InJitter = 1.f, int32_t InPeriodX = 0, int32_t InPeriodY = 0 );
}
