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
    };
}
