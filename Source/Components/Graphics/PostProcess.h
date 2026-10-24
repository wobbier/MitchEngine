#pragma once
#include "ECS/Component.h"
#include "ECS/ComponentDetail.h"
#include "Camera/PostProcessSettings.h"
#include "Path.h"
#include <string>
#include <vector>

// Image settings for the camera on the same entity: exposure, tonemapping, bloom, ambient
// occlusion, depth of field, colour grading, vignette and anti-aliasing. Cameras without one use the defaults.
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

    // Depth of field (perspective cameras).
    bool DepthOfField = false;
    float DoFFocusDistance = 8.f;
    float DoFAperture = 2.8f;
    float DoFSensorHeight = 24.f;
    float DoFMaxBlurRadius = 16.f;
    bool DoFDebugCoC = false;
    bool DoFSplitDiopter = false;
    float DoFSplitFocusDistance = 18.f;
    float DoFSplitPosition = 0.5f;
    float DoFSplitFeather = 0.015f;

    // Fog
    // Edge outlines (toon ink): depth silhouettes and crease folds over the opaque scene.
    bool Outline = false;
    Vector3 OutlineColor = Vector3( 0.f, 0.f, 0.f );
    float OutlineThickness = 1.f;
    float OutlineDepthThreshold = 0.1f;
    float OutlineNormalThreshold = 0.5f;

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
    // Display-referred LUT: a .cube file, or an image strip of N slices of NxN (give a strip
    // Format None, no mips). Stock grades ship in Engine/Assets/Textures/LUTs.
    Path Lut;
    float LutContribution = 1.f;

    // Vignette
    float VignetteIntensity = 0.2f;
    float VignetteSmoothness = 0.6f;

    bool FXAA = true;
    bool TemporalAA = false;
    float TemporalSharpness = 0.25f;

    // Post Process shader graphs, applied in order.
    std::vector<Path> Graphs;
    // Values for each graph's exposed properties, by graph (resolved alongside the graphs in
    // ToSettings, hence mutable).
    mutable Moonlight::ShaderGraphParamsList GraphParams;

#if USING( ME_EDITOR )
    // Keeps the Graph Properties in step with Graphs even when no camera uses this component.
    void OnEditorInspect() override;
#endif

private:
    // Loads Graphs (when they changed) into m_graphs and syncs GraphParams with them (main thread).
    void ResolveGraphs() const;

    // Loaded graphs by path (resolved on the main thread in ToSettings).
    mutable std::vector<std::pair<std::string, SharedPtr<Moonlight::ShaderGraphAsset>>> m_graphs;
    // The loaded LUT, by extension: .cube volumes, otherwise an image strip (main thread).
    mutable SharedPtr<Moonlight::ColorLut> m_lut;
    mutable SharedPtr<Moonlight::Texture> m_lutStrip;
    mutable std::string m_loadedLut;
};

ME_REGISTER_COMPONENT_FOLDER( PostProcess, "Rendering" )
