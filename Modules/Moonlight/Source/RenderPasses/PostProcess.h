#pragma once
#include <bgfx/bgfx.h>
#include "Camera/PostProcessSettings.h"

namespace Moonlight
{
    struct CameraData;
    struct FrameBuffer;
    class ViewAllocator;

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

        void Render( ViewAllocator& InViews, const CameraData& InCamera, FrameBuffer& InTargets, bgfx::FrameBufferHandle InOutput, uint16_t InOutputWidth, uint16_t InOutputHeight );

        // Seconds since the last frame, for eye adaptation.
        float DeltaSeconds = 1.f / 60.f;

    private:
        void SetupFullscreenView( bgfx::ViewId InView, bgfx::FrameBufferHandle InTarget, uint16_t InWidth, uint16_t InHeight );
        void SubmitFullscreen( bgfx::ViewId InView, bgfx::ProgramHandle InProgram, uint64_t InState = 0 );
        void RenderBloom( ViewAllocator& InViews, const PostProcessSettings& InSettings, FrameBuffer& InTargets );
        bgfx::TextureHandle RenderAutoExposure( ViewAllocator& InViews, const PostProcessSettings& InSettings, FrameBuffer& InTargets );

        bgfx::ProgramHandle m_tonemapProgram = BGFX_INVALID_HANDLE;
        bgfx::ProgramHandle m_fxaaProgram = BGFX_INVALID_HANDLE;
        bgfx::ProgramHandle m_bloomDownProgram = BGFX_INVALID_HANDLE;
        bgfx::ProgramHandle m_bloomUpProgram = BGFX_INVALID_HANDLE;
        bgfx::ProgramHandle m_ssaoProgram = BGFX_INVALID_HANDLE;
        bgfx::ProgramHandle m_ssaoBlurProgram = BGFX_INVALID_HANDLE;
        bgfx::ProgramHandle m_ssaoApplyProgram = BGFX_INVALID_HANDLE;
        bgfx::ProgramHandle m_luminanceProgram = BGFX_INVALID_HANDLE;
        bgfx::ProgramHandle m_adaptProgram = BGFX_INVALID_HANDLE;

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

        bgfx::TextureHandle m_blackTexture = BGFX_INVALID_HANDLE;
        bgfx::TextureHandle m_whiteTexture = BGFX_INVALID_HANDLE;
    };
}
