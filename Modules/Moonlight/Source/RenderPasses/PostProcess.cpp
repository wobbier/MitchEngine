#include "PostProcess.h"
#include "Camera/CameraData.h"
#include "Device/FrameBuffer.h"
#include "Primitives/Cube.h"
#include "RenderViews.h"
#include "Utils/BGFXUtils.h"
#include "optick.h"
#include <bx/math.h>
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace Moonlight
{
    namespace
    {
        constexpr uint64_t kWriteColor = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A;

        bgfx::ProgramHandle LoadPost( const char* InFragment )
        {
            return LoadProgram( "Assets/Shaders/Post/Fullscreen.vert", std::string( "Assets/Shaders/Post/" ) + InFragment + ".frag" );
        }

        void DestroyProgram( bgfx::ProgramHandle& InProgram )
        {
            if( bgfx::isValid( InProgram ) )
            {
                bgfx::destroy( InProgram );
            }
            InProgram = BGFX_INVALID_HANDLE;
        }

        void DestroyUniform( bgfx::UniformHandle& InUniform )
        {
            if( bgfx::isValid( InUniform ) )
            {
                bgfx::destroy( InUniform );
            }
            InUniform = BGFX_INVALID_HANDLE;
        }

        bgfx::TextureHandle MakeSolid( float InValue )
        {
            const float rgba[4] = { InValue, InValue, InValue, 1.f };
            return bgfx::createTexture2D( 1, 1, false, 1, bgfx::TextureFormat::RGBA32F, BGFX_SAMPLER_NONE, bgfx::copy( rgba, sizeof( rgba ) ) );
        }
    }


    PostProcess::PostProcess()
    {
        m_tonemapProgram = LoadPost( "Tonemap" );
        m_fxaaProgram = LoadPost( "FXAA" );
        m_bloomDownProgram = LoadPost( "BloomDown" );
        m_bloomUpProgram = LoadPost( "BloomUp" );
        m_ssaoProgram = LoadPost( "SSAO" );
        m_ssaoBlurProgram = LoadPost( "SSAOBlur" );
        m_ssaoApplyProgram = LoadPost( "SSAOApply" );
        m_luminanceProgram = LoadPost( "Luminance" );
        m_adaptProgram = LoadPost( "Adapt" );

        s_input0 = bgfx::createUniform( "s_input0", bgfx::UniformType::Sampler );
        s_input1 = bgfx::createUniform( "s_input1", bgfx::UniformType::Sampler );
        s_hdrColor = bgfx::createUniform( "s_hdrColor", bgfx::UniformType::Sampler );
        s_bloom = bgfx::createUniform( "s_bloom", bgfx::UniformType::Sampler );
        s_exposure = bgfx::createUniform( "s_exposure", bgfx::UniformType::Sampler );
        s_ldrColor = bgfx::createUniform( "s_ldrColor", bgfx::UniformType::Sampler );
        u_tonemap = bgfx::createUniform( "u_tonemap", bgfx::UniformType::Vec4 );
        u_grading = bgfx::createUniform( "u_grading", bgfx::UniformType::Vec4 );
        u_grading2 = bgfx::createUniform( "u_grading2", bgfx::UniformType::Vec4 );
        u_exposure = bgfx::createUniform( "u_exposure", bgfx::UniformType::Vec4 );
        u_bloom = bgfx::createUniform( "u_bloom", bgfx::UniformType::Vec4 );
        u_ssao = bgfx::createUniform( "u_ssao", bgfx::UniformType::Vec4 );
        u_ssaoProjection = bgfx::createUniform( "u_ssaoProjection", bgfx::UniformType::Vec4 );
        u_ssaoProjection2 = bgfx::createUniform( "u_ssaoProjection2", bgfx::UniformType::Vec4 );
        u_adapt = bgfx::createUniform( "u_adapt", bgfx::UniformType::Vec4 );

        m_blackTexture = MakeSolid( 0.f );
        m_whiteTexture = MakeSolid( 1.f );
    }


    PostProcess::~PostProcess()
    {
        for( bgfx::ProgramHandle* program : { &m_tonemapProgram, &m_fxaaProgram, &m_bloomDownProgram, &m_bloomUpProgram, &m_ssaoProgram, &m_ssaoBlurProgram, &m_ssaoApplyProgram, &m_luminanceProgram, &m_adaptProgram } )
        {
            DestroyProgram( *program );
        }
        for( bgfx::UniformHandle* uniform : { &s_input0, &s_input1, &s_hdrColor, &s_bloom, &s_exposure, &s_ldrColor, &u_tonemap, &u_grading, &u_grading2, &u_exposure, &u_bloom, &u_ssao, &u_ssaoProjection, &u_ssaoProjection2, &u_adapt } )
        {
            DestroyUniform( *uniform );
        }
        if( bgfx::isValid( m_blackTexture ) ) bgfx::destroy( m_blackTexture );
        if( bgfx::isValid( m_whiteTexture ) ) bgfx::destroy( m_whiteTexture );
    }


    void PostProcess::SetupFullscreenView( bgfx::ViewId InView, bgfx::FrameBufferHandle InTarget, uint16_t InWidth, uint16_t InHeight )
    {
        float ortho[16];
        bx::mtxOrtho( ortho, 0.f, 1.f, 1.f, 0.f, 0.f, 100.f, 0.f, bgfx::getCaps()->homogeneousDepth );
        bgfx::setViewFrameBuffer( InView, InTarget );
        bgfx::setViewRect( InView, 0, 0, InWidth, InHeight );
        bgfx::setViewTransform( InView, nullptr, ortho );
        bgfx::touch( InView );
    }


    void PostProcess::SubmitFullscreen( bgfx::ViewId InView, bgfx::ProgramHandle InProgram, uint64_t InState )
    {
        bgfx::setState( InState ? InState : kWriteColor );
        screenSpaceQuad( 1.f, 1.f, 0.f, bgfx::getCaps()->originBottomLeft );
        bgfx::submit( InView, InProgram );
    }


    void PostProcess::RenderAmbientOcclusion( ViewAllocator& InViews, const CameraData& InCamera, FrameBuffer& InTargets )
    {
        const PostProcessSettings& settings = InCamera.Post;
        if( !settings.AmbientOcclusion || settings.AOIntensity <= 0.f || !bgfx::isValid( InTargets.AOBuffer.Buffer ) )
        {
            return;
        }
        OPTICK_EVENT( "PostProcess::SSAO" );
        const glm::mat4& projection = InCamera.ProjectionMatrix.GetInternalMatrix();

        const bgfx::ViewId aoView = InViews.Allocate( "SSAO" );
        const bgfx::ViewId blurView = InViews.Allocate( "SSAO Blur" );
        const bgfx::ViewId applyView = InViews.Allocate( "SSAO Apply" );
        if( applyView == UINT16_MAX )
        {
            return;
        }

        SetupFullscreenView( aoView, InTargets.AOBuffer.Buffer, InTargets.AOBuffer.Width, InTargets.AOBuffer.Height );
        const float ssao[4] = { settings.AORadius, settings.AOIntensity, 0.002f, projection[1][1] };
        const float ssaoProjection[4] = { projection[2][2], projection[3][2], InCamera.Projection == ProjectionType::Orthographic ? 1.f : 0.f, bgfx::getCaps()->homogeneousDepth ? 1.f : 0.f };
        const float ssaoProjection2[4] = { projection[0][0], projection[1][1], 1.f / std::max<float>( InTargets.Width, 1.f ), 1.f / std::max<float>( InTargets.Height, 1.f ) };
        bgfx::setUniform( u_ssao, ssao );
        bgfx::setUniform( u_ssaoProjection, ssaoProjection );
        bgfx::setUniform( u_ssaoProjection2, ssaoProjection2 );
        bgfx::setTexture( 0, s_input0, InTargets.DepthTexture, BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP );
        SubmitFullscreen( aoView, m_ssaoProgram );

        SetupFullscreenView( blurView, InTargets.AOBlurBuffer.Buffer, InTargets.AOBlurBuffer.Width, InTargets.AOBlurBuffer.Height );
        bgfx::setTexture( 0, s_input0, InTargets.AOBuffer.Color );
        bgfx::setTexture( 1, s_input1, InTargets.DepthTexture, BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP );
        SubmitFullscreen( blurView, m_ssaoBlurProgram );

        // Multiply the opaque HDR colour by AO (dst * src).
        SetupFullscreenView( applyView, InTargets.SceneBuffer, static_cast<uint16_t>( InTargets.Width ), static_cast<uint16_t>( InTargets.Height ) );
        bgfx::setTexture( 0, s_input0, InTargets.AOBlurBuffer.Color );
        SubmitFullscreen( applyView, m_ssaoApplyProgram, BGFX_STATE_WRITE_RGB | BGFX_STATE_BLEND_FUNC( BGFX_STATE_BLEND_DST_COLOR, BGFX_STATE_BLEND_ZERO ) );
    }


    void PostProcess::RenderBloom( ViewAllocator& InViews, const PostProcessSettings& InSettings, FrameBuffer& InTargets )
    {
        OPTICK_EVENT( "PostProcess::Bloom" );
        const size_t mipCount = InTargets.BloomMips.size();
        // Downsample chain (the first pass thresholds).
        for( size_t i = 0; i < mipCount; ++i )
        {
            FrameBuffer::Mip& destination = InTargets.BloomMips[i];
            const bgfx::ViewId view = InViews.Allocate( "Bloom Down" );
            if( view == UINT16_MAX )
            {
                return;
            }
            SetupFullscreenView( view, destination.Buffer, destination.Width, destination.Height );
            const float bloom[4] = { InSettings.BloomThreshold, 0.5f, InSettings.BloomRadius, i == 0 ? 1.f : 0.f };
            bgfx::setUniform( u_bloom, bloom );
            bgfx::setTexture( 0, s_input0, i == 0 ? InTargets.SceneColor : InTargets.BloomMips[i - 1].Color );
            SubmitFullscreen( view, m_bloomDownProgram );
        }
        // Upsample, adding each level into the next larger one.
        for( size_t i = mipCount - 1; i >= 1; --i )
        {
            FrameBuffer::Mip& destination = InTargets.BloomMips[i - 1];
            const bgfx::ViewId view = InViews.Allocate( "Bloom Up" );
            if( view == UINT16_MAX )
            {
                return;
            }
            SetupFullscreenView( view, destination.Buffer, destination.Width, destination.Height );
            const float bloom[4] = { InSettings.BloomThreshold, 0.5f, InSettings.BloomRadius, 0.f };
            bgfx::setUniform( u_bloom, bloom );
            bgfx::setTexture( 0, s_input0, InTargets.BloomMips[i].Color );
            SubmitFullscreen( view, m_bloomUpProgram, BGFX_STATE_WRITE_RGB | BGFX_STATE_BLEND_ADD );
        }
    }


    bgfx::TextureHandle PostProcess::RenderAutoExposure( ViewAllocator& InViews, const PostProcessSettings& InSettings, FrameBuffer& InTargets )
    {
        OPTICK_EVENT( "PostProcess::AutoExposure" );
        const bgfx::ViewId luminanceView = InViews.Allocate( "Luminance" );
        const bgfx::ViewId adaptView = InViews.Allocate( "Eye Adaptation" );
        if( adaptView == UINT16_MAX )
        {
            return m_whiteTexture;
        }
        SetupFullscreenView( luminanceView, InTargets.LuminanceBuffer.Buffer, 1, 1 );
        bgfx::setTexture( 0, s_input0, InTargets.SceneColor );
        SubmitFullscreen( luminanceView, m_luminanceProgram );

        FrameBuffer::Mip& previous = InTargets.AdaptedLuminance[InTargets.AdaptedIndex];
        FrameBuffer::Mip& next = InTargets.AdaptedLuminance[1 - InTargets.AdaptedIndex];
        SetupFullscreenView( adaptView, next.Buffer, 1, 1 );
        // EV100 range -> luminance range (L = 2^EV * 12.5 / 100).
        const float minLuminance = std::pow( 2.f, InSettings.AutoExposureMinEV ) * 0.125f;
        const float maxLuminance = std::pow( 2.f, InSettings.AutoExposureMaxEV ) * 0.125f;
        const float adapt[4] = { 1.f - std::exp( -DeltaSeconds * InSettings.AutoExposureSpeed ), minLuminance, maxLuminance, 0.f };
        bgfx::setUniform( u_adapt, adapt );
        bgfx::setTexture( 0, s_input0, InTargets.LuminanceBuffer.Color );
        bgfx::setTexture( 1, s_input1, previous.Color );
        SubmitFullscreen( adaptView, m_adaptProgram );
        InTargets.AdaptedIndex = 1 - InTargets.AdaptedIndex;
        return next.Color;
    }


    void PostProcess::Render( ViewAllocator& InViews, const CameraData& InCamera, FrameBuffer& InTargets, bgfx::FrameBufferHandle InOutput, uint16_t InOutputWidth, uint16_t InOutputHeight )
    {
        OPTICK_EVENT( "PostProcess::Render" );
        const PostProcessSettings& settings = InCamera.Post;

        const bool bloom = settings.Bloom && settings.BloomIntensity > 0.f && !InTargets.BloomMips.empty();
        if( bloom )
        {
            RenderBloom( InViews, settings, InTargets );
        }
        const bgfx::TextureHandle exposureTexture = settings.AutoExposure ? RenderAutoExposure( InViews, settings, InTargets ) : m_whiteTexture;

        const bool fxaa = settings.FXAA;
        const bgfx::ViewId tonemapView = InViews.Allocate( "Tonemap" );
        if( tonemapView == UINT16_MAX )
        {
            return;
        }
        if( fxaa )
        {
            SetupFullscreenView( tonemapView, InTargets.PostBuffer, static_cast<uint16_t>( InTargets.Width ), static_cast<uint16_t>( InTargets.Height ) );
        }
        else
        {
            SetupFullscreenView( tonemapView, InOutput, InOutputWidth, InOutputHeight );
        }

        const float compensation = std::pow( 2.f, settings.ExposureCompensation );
        const float tonemap[4] = { compensation, static_cast<float>( settings.Operator ), bloom ? settings.BloomIntensity : 0.f, settings.VignetteIntensity };
        const float grading[4] = { settings.Saturation, settings.Contrast, settings.Temperature, settings.Tint };
        const float grading2[4] = { settings.VignetteSmoothness, settings.Gamma, settings.Lift, settings.Gain };
        const float exposure[4] = { settings.AutoExposure ? 1.f : 0.f, compensation, fxaa ? 1.f : 0.f, 0.f };
        bgfx::setUniform( u_tonemap, tonemap );
        bgfx::setUniform( u_grading, grading );
        bgfx::setUniform( u_grading2, grading2 );
        bgfx::setUniform( u_exposure, exposure );
        bgfx::setTexture( 0, s_hdrColor, InTargets.SceneColor );
        bgfx::setTexture( 1, s_bloom, bloom ? InTargets.BloomMips[0].Color : m_blackTexture );
        bgfx::setTexture( 2, s_exposure, exposureTexture );
        SubmitFullscreen( tonemapView, m_tonemapProgram );

        if( fxaa )
        {
            const bgfx::ViewId fxaaView = InViews.Allocate( "FXAA" );
            if( fxaaView == UINT16_MAX )
            {
                return;
            }
            SetupFullscreenView( fxaaView, InOutput, InOutputWidth, InOutputHeight );
            bgfx::setTexture( 0, s_ldrColor, InTargets.PostColor );
            SubmitFullscreen( fxaaView, m_fxaaProgram );
        }
    }
}
