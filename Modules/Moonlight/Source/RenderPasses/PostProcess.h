#pragma once
#include <bgfx/bgfx.h>
#include <functional>
#include "Camera/PostProcessSettings.h"
#include "Graphics/ShaderCommand.h"

namespace Moonlight
{
    struct CameraData;
    struct FrameBuffer;
    class ViewAllocator;

    // The fog uniforms shared by the fog pass and the forward shaders (Fog.sh): u_fogParams,
    // u_fogColor, u_fogVolume.
    void FogUniforms( const PostProcessSettings& InSettings, float OutParams[4], float OutColor[4], float OutVolume[4] );

    // The per-camera image pipeline after the HDR scene pass:
    //   [SSAO] -> [bloom downsample/upsample] -> tonemap + grading -> [FXAA] -> output
    // The output is the camera's final buffer, or the backbuffer for the game's main camera.
    class PostProcess
    {
    public:
        PostProcess();
        ~PostProcess();

        // Ambient occlusion from the scene depth, applied to the HDR colour before transparents.
        void RenderAmbientOcclusion( ViewAllocator& InViews, const CameraData& InCamera, FrameBuffer& InTargets );

        // Height fog over the opaque HDR colour (after SSAO, before transparents). InBindLighting
        // binds the renderer's lighting state for each fog draw: the lights, clusters and shadow
        // atlases volumetric fog raymarches, and the fog uniforms themselves (FogUniforms).
        void RenderFog( ViewAllocator& InViews, const CameraData& InCamera, FrameBuffer& InTargets, const std::function<void()>& InBindLighting );

        void Render( ViewAllocator& InViews, const CameraData& InCamera, FrameBuffer& InTargets, bgfx::FrameBufferHandle InOutput, uint16_t InOutputWidth, uint16_t InOutputHeight );

        // Seconds since the last frame, for eye adaptation.
        float DeltaSeconds = 1.f / 60.f;

    private:
        void SetupFullscreenView( bgfx::ViewId InView, bgfx::FrameBufferHandle InTarget, uint16_t InWidth, uint16_t InHeight );
        void SubmitFullscreen( bgfx::ViewId InView, const ShaderCommand& InProgram, uint64_t InState = 0 );
        void RenderBloom( ViewAllocator& InViews, const PostProcessSettings& InSettings, FrameBuffer& InTargets );
        bgfx::TextureHandle RenderAutoExposure( ViewAllocator& InViews, const PostProcessSettings& InSettings, FrameBuffer& InTargets );

        ShaderCommand m_tonemapProgram;
        ShaderCommand m_fxaaProgram;
        ShaderCommand m_bloomDownProgram;
        ShaderCommand m_bloomUpProgram;
        ShaderCommand m_ssaoProgram;
        ShaderCommand m_ssaoBlurProgram;
        ShaderCommand m_ssaoApplyProgram;
        ShaderCommand m_luminanceProgram;
        ShaderCommand m_adaptProgram;
        ShaderCommand m_fogScatterProgram;
        ShaderCommand m_fogBlurProgram;
        ShaderCommand m_fogProgram;

        bgfx::UniformHandle s_input0 = BGFX_INVALID_HANDLE;
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
        bgfx::UniformHandle u_ssaoProjection2 = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle u_adapt = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle u_fogProjection = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle u_fogProjection2 = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle u_fogScreen = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle u_fogInvView = BGFX_INVALID_HANDLE;

        bgfx::TextureHandle m_blackTexture = BGFX_INVALID_HANDLE;
        bgfx::TextureHandle m_whiteTexture = BGFX_INVALID_HANDLE;
    };
}
