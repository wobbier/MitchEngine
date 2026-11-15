#pragma once
#include <bgfx/bgfx.h>
#include <functional>
#include "Camera/PostProcessSettings.h"
#include "Graphics/ShaderCommand.h"
#include "Pointers.h"

namespace Moonlight
{
    struct CameraData;
    struct FrameBuffer;
    class ViewAllocator;
    class TextureHistogram;
    struct ProgramRef;

    // The fog uniforms shared by the fog pass and the forward shaders (Fog.sh): u_fogParams,
    // u_fogColor, u_fogVolume.
    void FogUniforms( const PostProcessSettings& InSettings, float OutParams[4], float OutColor[4], float OutVolume[4] );

    // The per-camera image pipeline after the HDR scene pass:
    //   [SSAO] -> [temporal AA] -> [depth of field] -> [bloom] -> tonemap + grading -> [FXAA] -> output
    // The output is the camera's final buffer, or the backbuffer for the game's main camera.
    class PostProcess
    {
    public:
        PostProcess();
        ~PostProcess();

        // Ambient occlusion from the scene depth, applied to the HDR colour before transparents.
        void RenderAmbientOcclusion( ViewAllocator& InViews, const CameraData& InCamera, FrameBuffer& InTargets );

        // Edge outlines (PostProcessSettings::Outline): depth and reconstructed-normal
        // discontinuities drawn over the opaque HDR colour, after SSAO so fog covers them too.
        void RenderOutlineEdges( ViewAllocator& InViews, const CameraData& InCamera, FrameBuffer& InTargets );

        // Height fog over the opaque HDR colour (after SSAO, before transparents). InBindLighting
        // binds the renderer's lighting state for each fog draw: the lights, clusters and shadow
        // atlases volumetric fog raymarches, and the fog uniforms themselves (FogUniforms).
        // With VolumetricTemporal the half-resolution march is resolved against its reprojected
        // history (InTargets.FogHistory, its own frame counter and previous matrix, so it works
        // without temporal AA) before the blur.
        // InFroxelFog (compute active and BGFXRenderer::EnableFroxelFog): volumetric fog is lit into
        // a camera-aligned froxel grid, blended with its history and integrated in compute, and the
        // apply pass reads that volume (ME-376). Otherwise, or while those shaders load, the march.
        void RenderFog( ViewAllocator& InViews, const CameraData& InCamera, FrameBuffer& InTargets, const std::function<void()>& InBindLighting, bool InFroxelFog = false );

        // Temporal anti-aliasing (PostProcessSettings::TemporalAA) once the HDR scene is complete:
        // resolves SceneColor against the reprojected history into InTargets.PostInput (SceneColor
        // when it's off). The renderer jitters the projection (InTargets.TemporalJitter) and fills
        // InTargets.ViewProjection before the scene draws.
        void RenderTemporalAA( ViewAllocator& InViews, const CameraData& InCamera, FrameBuffer& InTargets );

        // Copy the current graph input and resolve its mip chain before a graph that requests it.
        bgfx::TextureHandle RenderGraphSceneMips( ViewAllocator& InViews, FrameBuffer& InTargets, bgfx::TextureHandle InInput, float InThreshold = -1.f );

        void Render( ViewAllocator& InViews, const CameraData& InCamera, FrameBuffer& InTargets, bgfx::FrameBufferHandle InOutput, uint16_t InOutputWidth, uint16_t InOutputHeight, uint16_t InOutputX = 0, uint16_t InOutputY = 0 );

        // Era image effects (LOOK-S11): InTargets.EraInput through Kuwahara, palette and colour depth.
        void RenderEra( ViewAllocator& InViews, const CameraData& InCamera, FrameBuffer& InTargets, bgfx::FrameBufferHandle InOutput, uint16_t InOutputWidth, uint16_t InOutputHeight, uint16_t InOutputX = 0, uint16_t InOutputY = 0 );

        // Render scaling (LOOK-S1): InTargets.UpscaleInput (the internal-size result) stretched
        // into InOutput's rect with the camera's UpscaleFilter.
        void RenderUpscale( ViewAllocator& InViews, const CameraData& InCamera, FrameBuffer& InTargets, uint32_t InPixelFactor, bgfx::FrameBufferHandle InOutput, uint16_t InOutputWidth, uint16_t InOutputHeight, uint16_t InOutputX = 0, uint16_t InOutputY = 0 );

        // An overlay camera's final image (Camera::Overlay) over InOutput, the main camera's: it
        // replaces the scene where the overlay's depth shows it drew, and adds on top elsewhere.
        void RenderOverlayComposite( ViewAllocator& InViews, const CameraData& InOverlayCamera, const FrameBuffer& InOverlay, bgfx::FrameBufferHandle InOutput, uint16_t InOutputWidth, uint16_t InOutputHeight );

        // The sub-pixel jitter (in pixels, -0.5..0.5) of frame InFrame of the temporal sequence
        // (Halton 2, 3 over 8 frames).
        static void TemporalJitter( uint32_t InFrame, float OutJitter[2] );
        // How much of the history each resolve keeps.
        static constexpr float kTemporalHistoryWeight = 0.9f;
        // How much of the fog history each volumetric fog resolve keeps (VolumetricTemporal).
        static constexpr float kFogHistoryWeight = 0.9f;
        // The most raymarch steps a volumetric fog pixel takes (FogScatter.frag's loop bound).
        static constexpr int kMaxVolumetricSteps = 256;
        // The most samples a local light takes along a ray with VolumetricLightSamples
        // (FogScatterLights.frag's FOG_MAX_LIGHT_SAMPLES).
        static constexpr int kMaxVolumetricLightSamples = 32;
        // Froxel fog grid: FroxelGridRows rows (a fixed angle per cell, whatever the resolution),
        // columns to keep the cells square (16..kFroxelMaxColumns), and VolumetricSteps slices deep
        // (kFroxelMinDepth..kFroxelMaxDepth, FogFroxelIntegrate's bound).
        static constexpr int kFroxelGridRows = 90;
        static constexpr int kFroxelMaxColumns = 512;
        static constexpr int kFroxelMinDepth = 8;
        static constexpr int kFroxelMaxDepth = 128;
        // The froxel grid's nearest boundary never comes closer than this (metres of view depth): the
        // exponential slices would otherwise spend themselves in the first metre. Fog in front of it
        // is lit with the first slice.
        static constexpr float kFroxelNearDepth = 0.5f;

        // Seconds since the last frame, for eye adaptation.
        float DeltaSeconds = 1.f / 60.f;
        // Auto exposure meters on a luminance histogram built in compute (AutoExposureLow/HighPercentile)
        // instead of the log-average chain. The renderer sets it from IsComputeActive() each frame;
        // --no-histogram-exposure keeps the chain, for comparing the two.
        bool UseComputeMetering = false;
        // Froxel fog tuning, for comparing (set from the command line): --froxel-grid-rows N (16..256),
        // --froxel-slices N (0: VolumetricSteps), --froxel-near M (metres, kFroxelNearDepth).
        int FroxelGridRows = kFroxelGridRows;
        int FroxelSlices = 0;
        float FroxelNearDepth = kFroxelNearDepth;
        // A render texture's target on a backend whose framebuffers start at the bottom (OpenGL):
        // passes writing it flip vertically, so it holds its top row first like an image.
        bgfx::FrameBufferHandle FlippedTarget = BGFX_INVALID_HANDLE;

    private:
        void SetupFullscreenView( bgfx::ViewId InView, bgfx::FrameBufferHandle InTarget, uint16_t InWidth, uint16_t InHeight, uint16_t InX = 0, uint16_t InY = 0 );
        void SubmitFullscreen( bgfx::ViewId InView, const ShaderCommand& InProgram, uint64_t InState = 0 );
        void RenderBloom( ViewAllocator& InViews, const PostProcessSettings& InSettings, FrameBuffer& InTargets );
        void RenderDepthOfField( ViewAllocator& InViews, const CameraData& InCamera, FrameBuffer& InTargets );
        // The histogram path of RenderAutoExposure: bins the HDR image, averages between the
        // percentiles into m_meteredLuminance. False (nothing submitted) while a shader loads.
        bool RenderHistogramLuminance( ViewAllocator& InViews, const PostProcessSettings& InSettings, FrameBuffer& InTargets, bgfx::TextureHandle& OutLuminance );
        bgfx::TextureHandle RenderAutoExposure( ViewAllocator& InViews, const PostProcessSettings& InSettings, FrameBuffer& InTargets );

        ShaderCommand m_tonemapProgram;
        ShaderCommand m_fxaaProgram;
        ShaderCommand m_bloomDownProgram;
        ShaderCommand m_bloomUpProgram;
        ShaderCommand m_ssaoProgram;
        ShaderCommand m_ssaoBlurProgram;
        ShaderCommand m_ssaoApplyProgram;
        ShaderCommand m_outlineProgram;
        ShaderCommand m_luminanceProgram;
        ShaderCommand m_adaptProgram;
        ShaderCommand m_fogScatterProgram;
        ShaderCommand m_fogScatterLightsProgram;    // VolumetricLightSamples: each local light on its own stretch of the ray
        ShaderCommand m_fogBlurProgram;
        ShaderCommand m_fogResolveProgram;
        ShaderCommand m_fogProgram;
        ShaderCommand m_fogFroxelProgram;           // the froxel path's apply (FogFroxel.frag)
        ShaderCommand m_taaProgram;
        ShaderCommand m_copyProgram;
        ShaderCommand m_upscaleProgram;
        ShaderCommand m_overlayProgram;
        ShaderCommand m_eraProgram;
        ShaderCommand m_dofCoCProgram;
        ShaderCommand m_dofPrefilterProgram;
        ShaderCommand m_dofDilateProgram;
        ShaderCommand m_dofGatherProgram;
        ShaderCommand m_dofCompositeProgram;

        bgfx::UniformHandle s_dof0 = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle s_dof1 = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle s_dof2 = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle s_dof3 = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle s_dof4 = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle u_dofLens = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle u_dofSplit = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle u_dofProjection = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle u_dofScreen = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle u_dofPass = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle u_dofJitter = BGFX_INVALID_HANDLE;

        bgfx::UniformHandle s_input0 = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle u_upscaleParams = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle u_upscaleSize = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle u_overlayParams = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle u_overlayFade = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle u_overlayLook = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle u_eraPost = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle s_input1 = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle s_hdrColor = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle s_bloom = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle s_exposure = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle s_ldrColor = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle u_tonemap = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle u_grading = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle u_grading2 = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle u_exposure = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle u_bloom = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle u_ssao = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle u_ssaoProjection = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle u_ssaoScreen = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle u_ssaoInvProjection = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle u_adapt = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle u_fogProjection = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle u_fogProjection2 = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle u_fogScreen = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle u_fogInvView = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle u_fogVolume2 = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle u_fogScatter = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle u_fogReproject = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle s_fogHistory = BGFX_INVALID_HANDLE;
        // Froxel fog (FogFroxel.sh); the compute programs are acquired on the first froxel frame.
        bgfx::UniformHandle u_froxelGrid = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle u_froxelDepth = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle u_froxelParams = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle u_froxelPrevView = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle s_froxelRaw = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle s_froxelHistory = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle s_froxelIn = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle s_froxelVolume = BGFX_INVALID_HANDLE;
        SharedPtr<ProgramRef> m_froxelInjectProgram;
        SharedPtr<ProgramRef> m_froxelBlendProgram;
        SharedPtr<ProgramRef> m_froxelIntegrateProgram;
        bool m_loggedFroxelFog = false;
        bgfx::UniformHandle s_taaCurrent = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle s_taaHistory = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle s_taaDepth = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle u_taaParams = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle u_taaJitter = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle u_taaReproject = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle u_graphSceneFilter = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle u_outline = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle u_outlineParams = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle s_lut = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle s_lutStrip = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle u_lut = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle u_grain = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle u_lens = BGFX_INVALID_HANDLE;
        // Advances every tonemap pass: the grain's pattern when a camera doesn't pick one.
        uint32_t m_grainCounter = 0;

        bgfx::TextureHandle m_blackTexture = BGFX_INVALID_HANDLE;
        bgfx::TextureHandle m_whiteTexture = BGFX_INVALID_HANDLE;
        // Compute metering, made on first use (only where compute is active).
        UniquePtr<TextureHistogram> m_histogram;
        SharedPtr<ProgramRef> m_exposureHistogramProgram;
        bgfx::TextureHandle m_meteredLuminance = BGFX_INVALID_HANDLE;   // 1x1 R32F the compute pass writes
        bgfx::UniformHandle u_meterParams = BGFX_INVALID_HANDLE;
        bool m_histogramMeteringDisabled = false;
        // Bound to the volume LUT slot when no LUT is active (samplers must always be bound).
        bgfx::TextureHandle m_identityVolume = BGFX_INVALID_HANDLE;
    };
}
