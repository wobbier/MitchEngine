#pragma once
#include <cmath>
#include <cstdint>
#include <vector>
#include "Pointers.h"
#include "Materials/ShaderGraphParams.h"

namespace Moonlight
{
    class ShaderGraphAsset;
    class ColorLut;
    class Texture;

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
        // Metering on a compute-built luminance histogram averages only between these fractions of
        // the pixels, darkest first (the log-average fallback ignores them).
        float AutoExposureLowPercentile = 0.1f;
        float AutoExposureHighPercentile = 0.9f;

        Tonemapper Operator = Tonemapper::ACES;

        // Bloom
        bool Bloom = true;
        float BloomIntensity = 0.05f;
        float BloomThreshold = 1.0f;        // luminance where bloom starts (soft knee)
        float BloomRadius = 1.0f;           // upsample filter scale

        // Ambient occlusion
        bool AmbientOcclusion = true;
        float AORadius = 0.5f;              // metres
        float AOIntensity = 1.0f;

        // Thin-lens depth of field (perspective cameras, before bloom/tonemapping).
        bool DepthOfField = false;
        float DoFFocusDistance = 8.f;       // metres along the camera's forward axis
        float DoFAperture = 2.8f;           // f-number; smaller = shallower focus
        float DoFSensorHeight = 24.f;      // mm; focal length follows the camera's vertical FOV
        float DoFMaxBlurRadius = 16.f;      // radius in pixels at 1080p (scales with output height)
        bool DoFDebugCoC = false;          // red foreground, green background, black in focus
        bool DoFSplitDiopter = false;
        float DoFSplitFocusDistance = 18.f;
        float DoFSplitPosition = 0.5f;      // normalized screen X
        float DoFSplitFeather = 0.015f;     // full transition width, normalized to image width

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
        int VolumetricSteps = 24;           // up to 256
        bool VolumetricTemporal = true;     // accumulate the march over frames (own reprojected history, independent of TemporalAA)
        float FogLocalAnisotropy = 0.f;     // Henyey-Greenstein g for point / spot / area lights in the fog (0 even)
        // Samples each local light takes along the part of a ray inside its range and cone (up to
        // 32). 0 lights every march step from the step's cluster instead, whose cost is steps x
        // the cluster's lights: with many long, narrow beams in one room nearly every light is in
        // nearly every cluster, and almost all of that work finds nothing.
        int VolumetricLightSamples = 0;

        // Edge outlines over the opaque scene: depth silhouettes and crease folds, from depth alone.
        bool Outline = false;
        float OutlineColor[3] = { 0.f, 0.f, 0.f };  // sRGB
        float OutlineThickness = 1.f;               // sample offset in pixels
        float OutlineDepthThreshold = 0.1f;         // view-depth gap per metre of depth
        float OutlineNormalThreshold = 0.5f;        // crease fold (0 everything .. 2 nothing)

        // Colour grading
        float Saturation = 1.f;
        float Contrast = 1.f;
        float Temperature = 0.f;            // -1 cool .. 1 warm
        float Tint = 0.f;                   // -1 green .. 1 magenta
        float Gamma = 1.f;
        float Lift = 0.f;
        float Gain = 1.f;
        // Display-referred grading LUT: a .cube volume, or an image strip of N slices of NxN.
        SharedPtr<ColorLut> Lut;
        SharedPtr<Texture> LutStrip;
        float LutContribution = 1.f;

        float VignetteIntensity = 0.2f;
        float VignetteSmoothness = 0.6f;

        // Era effects (LOOK-S11). On the geometry (Standard materials): vertices snap to a grid
        // EraVertexSnap virtual pixels tall (PS1 wobble), affine texture mapping (PS1 warping), and
        // three-point texture filtering (N64). On the image, at the render size, before upscaling:
        // a Kuwahara painterly filter (radius in pixels), colour depth (bits per channel, 5 = 15-bit)
        // with ordered dithering, and quantizing to a palette image (one row of colours).
        float EraVertexSnap = 0.f;
        bool EraAffineTextures = false;
        bool EraThreePointFilter = false;
        int EraKuwahara = 0;
        int EraColorBits = 0;
        float EraDither = 0.f;
        SharedPtr<Texture> EraPalette;
        bool HasEraImageEffects() const { return EraKuwahara > 0 || EraColorBits > 0 || EraPalette != nullptr; }

        // Film grain, in the tonemap pass after grading and the LUT: monochrome noise of
        // FilmGrain strength (0 = off), strongest in the midtones, in cells FilmGrainSize screen
        // pixels across. FilmGrainFrame picks the pattern: -1 is a new one every rendered frame, a
        // fixed number holds it (a game stepping its image at a low frame rate steps the grain too).
        float FilmGrain = 0.f;
        float FilmGrainSize = 1.5f;
        int FilmGrainFrame = -1;

        // Lens distortion, in the tonemap pass: the picture is bent like a fisheye's (straight
        // lines bow outward, the middle swells), 0 = off .. 1 = strongest. The corners stay the
        // picture's corners, so the middle is magnified by LensDistortionZoom: a camera that
        // widens its field of view by that keeps what it framed. LensFringe (0..1) splits the
        // colours toward the edges, like cheap glass.
        float LensDistortion = 0.f;
        float LensFringe = 0.f;
        // The half angle (radians) the picture's diagonal is bent over; Tonemap.frag has the map.
        static float LensDistortionAngle( float inDistortion ) { return ( inDistortion < 0.f ? 0.f : inDistortion > 1.f ? 1.f : inDistortion ) * 1.35f; }
        // How much the middle of the picture is magnified at inDistortion.
        static float LensDistortionZoom( float inDistortion )
        {
            const float angle = LensDistortionAngle( inDistortion );
            return angle > 1e-3f ? std::tan( angle ) / angle : 1.f;
        }

        bool FXAA = true;
        // Temporal anti-aliasing: the projection is jittered by a sub-pixel each frame and the HDR
        // image is resolved against the reprojected history (camera motion through the depth).
        bool TemporalAA = false;
        float TemporalSharpness = 0.25f;    // counters the resolve's softening (0 = none)

        // Post Process shader graphs, in order (each runs before or after tonemapping per its
        // Injection setting).
        std::vector<SharedPtr<ShaderGraphAsset>> Graphs;
        // Each graph's property values (parallel to Graphs).
        std::vector<ShaderGraphBinding> GraphBindings;
    };
}
