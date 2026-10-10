#pragma once
#include "ECS/Component.h"
#include "ECS/ComponentDetail.h"
#include "Camera/PostProcessSettings.h"

// Image settings for the camera on the same entity: exposure, tonemapping, bloom, ambient
// occlusion, colour grading, vignette and anti-aliasing. Cameras without one use the defaults.
// The editor's scene view uses the main camera's settings.
class PostProcess
    : public Component<PostProcess>
{
    ME_REFLECTABLE( PostProcess )
public:
    PostProcess();

    Moonlight::PostProcessSettings ToSettings() const;

    // Exposure
    float ExposureCompensation = 0.f;
    bool AutoExposure = false;
    float AutoExposureMinEV = -4.f;
    float AutoExposureMaxEV = 8.f;
    float AutoExposureSpeed = 2.f;
    Moonlight::Tonemapper Tonemapper = Moonlight::Tonemapper::ACES;

    // Bloom
    bool Bloom = true;
    float BloomIntensity = 0.05f;
    float BloomThreshold = 1.f;
    float BloomRadius = 1.f;

    // Ambient occlusion
    bool AmbientOcclusion = true;
    float AORadius = 0.5f;
    float AOIntensity = 1.f;

    // Fog
    bool Fog = false;
    Vector3 FogColor = Vector3( 0.55f, 0.62f, 0.72f );
    float FogDensity = 0.02f;
    float FogHeight = 0.f;
    float FogHeightFalloff = 0.15f;
    float FogStartDistance = 0.f;
    float FogMaxOpacity = 1.f;
    float FogSunIntensity = 1.f;
    float FogAnisotropy = 0.6f;
    bool VolumetricFog = false;
    float VolumetricDistance = 60.f;
    int VolumetricSteps = 24;

    // Colour grading
    float Saturation = 1.f;
    float Contrast = 1.f;
    float Temperature = 0.f;
    float Tint = 0.f;
    float Gamma = 1.f;
    float Lift = 0.f;
    float Gain = 1.f;

    // Vignette
    float VignetteIntensity = 0.2f;
    float VignetteSmoothness = 0.6f;

    bool FXAA = true;
    bool TemporalAA = false;
    float TemporalSharpness = 0.25f;
};

ME_REGISTER_COMPONENT_FOLDER( PostProcess, "Rendering" )
