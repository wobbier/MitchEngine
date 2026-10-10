#pragma once
#include <cstdint>

namespace Moonlight
{
    enum class Tonemapper : uint8_t
    {
        None = 0,   // clamp
        ACES,
        AgX,
        Reinhard,
    };

    // Per-camera image settings (from the camera's PostProcess component, or defaults).
    struct PostProcessSettings
    {
        // Exposure
        float ExposureCompensation = 0.f;   // stops (EV) added on top of auto exposure
        bool AutoExposure = false;
        float AutoExposureMinEV = -4.f;
        float AutoExposureMaxEV = 8.f;
        float AutoExposureSpeed = 2.f;      // adaptation rate (1/s)

        Tonemapper Operator = Tonemapper::ACES;

        // Bloom
        bool Bloom = true;
        float BloomIntensity = 0.05f;
        float BloomThreshold = 1.0f;        // luminance where bloom starts (soft knee)
        float BloomRadius = 1.0f;           // upsample filter scale

        // Ambient occlusion
        bool AmbientOcclusion = true;
        float AORadius = 0.5f;              // world units
        float AOIntensity = 1.0f;

        // Exponential height fog (applied before transparents)
        bool Fog = false;
        float FogColor[3] = { 0.55f, 0.62f, 0.72f };    // ambient in-scattering, sRGB (HDR allowed)
        float FogDensity = 0.02f;           // extinction per metre at the base height
        float FogHeight = 0.f;              // world Y of the base height
        float FogHeightFalloff = 0.15f;     // density falls by e every 1/falloff metres above it
        float FogStartDistance = 0.f;       // metres from the camera before fog begins
        float FogMaxOpacity = 1.f;
        float FogSunIntensity = 1.f;        // scale of the sun (and, when volumetric, local lights) in the fog
        float FogAnisotropy = 0.6f;         // Henyey-Greenstein g: 0 even, towards 1 glows around the sun
        // Raymarched lighting: light shafts through the sun's shadows, point and spot lights.
        bool VolumetricFog = false;
        float VolumetricDistance = 60.f;    // metres raymarched (analytic beyond)
        int VolumetricSteps = 24;

        // Colour grading
        float Saturation = 1.f;
        float Contrast = 1.f;
        float Temperature = 0.f;            // -1 cool .. 1 warm
        float Tint = 0.f;                   // -1 green .. 1 magenta
        float Gamma = 1.f;
        float Lift = 0.f;
        float Gain = 1.f;

        float VignetteIntensity = 0.2f;
        float VignetteSmoothness = 0.6f;

        bool FXAA = true;
        // Temporal anti-aliasing: the projection is jittered by a sub-pixel each frame and the HDR
        // image is resolved against the reprojected history (camera motion through the depth).
        bool TemporalAA = false;
        float TemporalSharpness = 0.25f;    // counters the resolve's softening (0 = none)
    };
}
