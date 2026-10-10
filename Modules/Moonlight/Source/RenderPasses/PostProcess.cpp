#include "PostProcess.h"
#include "Camera/CameraData.h"
#include "Device/FrameBuffer.h"
#include "Primitives/Cube.h"
#include "RenderViews.h"
#include "Utils/BGFXUtils.h"
#include "optick.h"
#include <bx/math.h>
#include <glm/gtc/type_ptr.hpp>
#include <glm/mat4x4.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace Moonlight
{
    namespace
    {
        constexpr uint64_t kWriteColor = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A;

        // Hot reloadable: the shared program is rebuilt when either shader changes.
        ShaderCommand LoadPost( const char* InFragment )
        {
            return ShaderCommand( "Assets/Shaders/Post/Fullscreen", std::string( "Assets/Shaders/Post/" ) + InFragment );
        }

        void DestroyUniform( bgfx::UniformHandle& InUniform )
        {
            if( bgfx::isValid( InUniform ) )
            {
                bgfx::destroy( InUniform );
            }
            InUniform = BGFX_INVALID_HANDLE;
        }

        float SrgbToLinear( float InValue )
        {
            return InValue <= 0.04045f ? InValue / 12.92f : std::pow( ( InValue + 0.055f ) / 1.055f, 2.4f );
        }


        bgfx::TextureHandle MakeSolid( float InValue )
        {
            const float rgba[4] = { InValue, InValue, InValue, 1.f };
            return bgfx::createTexture2D( 1, 1, false, 1, bgfx::TextureFormat::RGBA32F, BGFX_SAMPLER_NONE, bgfx::copy( rgba, sizeof( rgba ) ) );
        }
    }


    void FogUniforms( const PostProcessSettings& InSettings, float OutParams[4], float OutColor[4], float OutVolume[4] )
    {
        const bool volumetric = InSettings.VolumetricFog && InSettings.VolumetricSteps > 0;
        OutParams[0] = InSettings.FogDensity;
        OutParams[1] = InSettings.FogHeight;
        OutParams[2] = std::max( InSettings.FogHeightFalloff, 0.f );
        OutParams[3] = std::max( InSettings.FogStartDistance, 0.f );
        OutColor[0] = SrgbToLinear( InSettings.FogColor[0] );
        OutColor[1] = SrgbToLinear( InSettings.FogColor[1] );
        OutColor[2] = SrgbToLinear( InSettings.FogColor[2] );
        OutColor[3] = std::clamp( InSettings.FogMaxOpacity, 0.f, 1.f );
        OutVolume[0] = InSettings.FogSunIntensity;
        OutVolume[1] = std::clamp( InSettings.FogAnisotropy, -0.95f, 0.95f );
        OutVolume[2] = std::max( InSettings.VolumetricDistance, 1.f );
        OutVolume[3] = volumetric ? static_cast<float>( std::min( InSettings.VolumetricSteps, 64 ) ) : 0.f;
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
        m_fogScatterProgram = LoadPost( "FogScatter" );
        m_fogBlurProgram = LoadPost( "FogBlur" );
        m_fogProgram = LoadPost( "Fog" );
        m_taaProgram = LoadPost( "TAA" );

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
        u_fogProjection = bgfx::createUniform( "u_fogProjection", bgfx::UniformType::Vec4 );
        u_fogProjection2 = bgfx::createUniform( "u_fogProjection2", bgfx::UniformType::Vec4 );
        u_fogScreen = bgfx::createUniform( "u_fogScreen", bgfx::UniformType::Vec4 );
        u_fogInvView = bgfx::createUniform( "u_fogInvView", bgfx::UniformType::Mat4 );
        s_taaCurrent = bgfx::createUniform( "s_taaCurrent", bgfx::UniformType::Sampler );
        s_taaHistory = bgfx::createUniform( "s_taaHistory", bgfx::UniformType::Sampler );
        s_taaDepth = bgfx::createUniform( "s_taaDepth", bgfx::UniformType::Sampler );
        u_taaParams = bgfx::createUniform( "u_taaParams", bgfx::UniformType::Vec4 );
        u_taaJitter = bgfx::createUniform( "u_taaJitter", bgfx::UniformType::Vec4 );
        u_taaReproject = bgfx::createUniform( "u_taaReproject", bgfx::UniformType::Mat4 );

        m_blackTexture = MakeSolid( 0.f );
        m_whiteTexture = MakeSolid( 1.f );
    }


    PostProcess::~PostProcess()
    {
        for( bgfx::UniformHandle* uniform : { &s_input0, &s_input1, &s_hdrColor, &s_bloom, &s_exposure, &s_ldrColor, &u_tonemap, &u_grading, &u_grading2, &u_exposure, &u_bloom, &u_ssao, &u_ssaoProjection, &u_ssaoProjection2, &u_adapt,
            &u_fogProjection, &u_fogProjection2, &u_fogScreen, &u_fogInvView, &s_taaCurrent, &s_taaHistory, &s_taaDepth, &u_taaParams, &u_taaJitter, &u_taaReproject } )
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


    void PostProcess::SubmitFullscreen( bgfx::ViewId InView, const ShaderCommand& InProgram, uint64_t InState )
    {
        bgfx::setState( InState ? InState : kWriteColor );
        screenSpaceQuad( 1.f, 1.f, 0.f, bgfx::getCaps()->originBottomLeft );
        bgfx::submit( InView, InProgram.GetProgram() );
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


    void PostProcess::RenderFog( ViewAllocator& InViews, const CameraData& InCamera, FrameBuffer& InTargets, const std::function<void()>& InBindLighting )
    {
        const PostProcessSettings& settings = InCamera.Post;
        if( !settings.Fog || settings.FogDensity <= 0.f || settings.FogMaxOpacity <= 0.f || !bgfx::isValid( InTargets.FogBuffer.Buffer ) || !bgfx::isValid( InTargets.ParticleBuffer ) )
        {
            return;
        }
        OPTICK_EVENT( "PostProcess::Fog" );
        const glm::mat4& projection = InCamera.ProjectionMatrix.GetInternalMatrix();
        const glm::mat4 invView = glm::inverse( InCamera.View.GetInternalMatrix() );

        float fogParams[4];
        float fogColor[4];
        float fogVolume[4];
        FogUniforms( settings, fogParams, fogColor, fogVolume );
        const bool volumetric = fogVolume[3] > 0.f;
        const float fogProjection[4] = { projection[2][2], projection[3][2], InCamera.Projection == ProjectionType::Orthographic ? 1.f : 0.f, bgfx::getCaps()->homogeneousDepth ? 1.f : 0.f };
        const float fogProjection2[4] = { projection[0][0], projection[1][1], InCamera.Far, 0.f };
        const float fogScreen[4] = { static_cast<float>( InTargets.Width ), static_cast<float>( InTargets.Height ), static_cast<float>( InTargets.FogBuffer.Width ), static_cast<float>( InTargets.FogBuffer.Height ) };
        const uint64_t pointClamp = BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
        auto setFogState = [&]() {
            InBindLighting();
            bgfx::setUniform( u_fogProjection, fogProjection );
            bgfx::setUniform( u_fogProjection2, fogProjection2 );
            bgfx::setUniform( u_fogScreen, fogScreen );
            bgfx::setUniform( u_fogInvView, &invView[0][0] );
        };

        if( volumetric )
        {
            const bgfx::ViewId scatterView = InViews.Allocate( "Fog Scatter" );
            const bgfx::ViewId blurView = InViews.Allocate( "Fog Blur" );
            if( blurView == UINT16_MAX )
            {
                return;
            }
            SetupFullscreenView( scatterView, InTargets.FogBuffer.Buffer, InTargets.FogBuffer.Width, InTargets.FogBuffer.Height );
            setFogState();
            bgfx::setTexture( 0, s_input0, InTargets.DepthTexture, pointClamp );
            SubmitFullscreen( scatterView, m_fogScatterProgram );

            SetupFullscreenView( blurView, InTargets.FogBlurBuffer.Buffer, InTargets.FogBlurBuffer.Width, InTargets.FogBlurBuffer.Height );
            setFogState();
            bgfx::setTexture( 0, s_input0, InTargets.FogBuffer.Color );
            bgfx::setTexture( 1, s_input1, InTargets.DepthTexture, pointClamp );
            SubmitFullscreen( blurView, m_fogBlurProgram );
        }

        // Over the opaque HDR colour (the depth-less target, so depth can be read): src + dst * src.a.
        const bgfx::ViewId applyView = InViews.Allocate( "Fog" );
        if( applyView == UINT16_MAX )
        {
            return;
        }
        SetupFullscreenView( applyView, InTargets.ParticleBuffer, static_cast<uint16_t>( InTargets.Width ), static_cast<uint16_t>( InTargets.Height ) );
        setFogState();
        bgfx::setTexture( 0, s_input0, volumetric ? InTargets.FogBlurBuffer.Color : m_blackTexture );
        bgfx::setTexture( 1, s_input1, InTargets.DepthTexture, pointClamp );
        SubmitFullscreen( applyView, m_fogProgram, BGFX_STATE_WRITE_RGB | BGFX_STATE_BLEND_FUNC( BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_SRC_ALPHA ) );
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
            bgfx::setTexture( 0, s_input0, i == 0 ? InTargets.PostInput : InTargets.BloomMips[i - 1].Color );
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
        bgfx::setTexture( 0, s_input0, InTargets.PostInput );
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


    void PostProcess::TemporalJitter( uint32_t InFrame, float OutJitter[2] )
    {
        auto halton = []( uint32_t InIndex, uint32_t InBase ) {
            float result = 0.f;
            float fraction = 1.f / static_cast<float>( InBase );
            for( uint32_t i = InIndex; i > 0; i /= InBase )
            {
                result += fraction * static_cast<float>( i % InBase );
                fraction /= static_cast<float>( InBase );
            }
            return result;
        };
        const uint32_t index = InFrame % 8u + 1u;
        OutJitter[0] = halton( index, 2 ) - 0.5f;
        OutJitter[1] = halton( index, 3 ) - 0.5f;
    }


    void PostProcess::RenderTemporalAA( ViewAllocator& InViews, const CameraData& InCamera, FrameBuffer& InTargets )
    {
        InTargets.PostInput = InTargets.SceneColor;
        if( !InCamera.Post.TemporalAA )
        {
            InTargets.TemporalValid = false;
            InTargets.TemporalFrame = 0;
            return;
        }
        OPTICK_EVENT( "PostProcess::TemporalAA" );
        InTargets.EnsureTemporalTargets();
        const bgfx::ViewId view = InViews.Allocate( "Temporal AA" );
        if( view == UINT16_MAX )
        {
            return;
        }
        const uint32_t read = InTargets.TemporalIndex;
        const uint32_t write = 1 - read;
        FrameBuffer::Mip& target = InTargets.TemporalHistory[write];
        SetupFullscreenView( view, target.Buffer, target.Width, target.Height );

        // Unjittered clip space now -> last frame's clip space (camera motion; objects that move on
        // their own rely on the neighbourhood clip).
        const glm::mat4 current = glm::make_mat4( InTargets.ViewProjection );
        const glm::mat4 previous = glm::make_mat4( InTargets.PreviousViewProjection );
        const glm::mat4 reproject = previous * glm::inverse( current );
        const float params[4] = { kTemporalHistoryWeight, InTargets.TemporalValid ? 1.f : 0.f, std::clamp( InCamera.Post.TemporalSharpness, 0.f, 1.f ), bgfx::getCaps()->homogeneousDepth ? 1.f : 0.f };
        // The scene drew shifted by the jitter (NDC); in uv (top-left origin) that's half, y flipped.
        const float jitter[4] = { InTargets.TemporalJitter[0] * 0.5f, -InTargets.TemporalJitter[1] * 0.5f, 0.f, 0.f };
        bgfx::setUniform( u_taaParams, params );
        bgfx::setUniform( u_taaJitter, jitter );
        bgfx::setUniform( u_taaReproject, &reproject[0][0] );
        bgfx::setTexture( 0, s_taaCurrent, InTargets.SceneColor );
        bgfx::setTexture( 1, s_taaHistory, InTargets.TemporalValid ? InTargets.TemporalHistory[read].Color : InTargets.SceneColor );
        bgfx::setTexture( 2, s_taaDepth, InTargets.DepthTexture );
        SubmitFullscreen( view, m_taaProgram );

        InTargets.TemporalIndex = write;
        InTargets.TemporalValid = true;
        std::copy( InTargets.ViewProjection, InTargets.ViewProjection + 16, InTargets.PreviousViewProjection );
        ++InTargets.TemporalFrame;
        InTargets.PostInput = target.Color;
    }


    void PostProcess::Render( ViewAllocator& InViews, const CameraData& InCamera, FrameBuffer& InTargets, bgfx::FrameBufferHandle InOutput, uint16_t InOutputWidth, uint16_t InOutputHeight )
    {
        OPTICK_EVENT( "PostProcess::Render" );
        const PostProcessSettings& settings = InCamera.Post;
        if( !bgfx::isValid( InTargets.PostInput ) )
        {
            InTargets.PostInput = InTargets.SceneColor;
        }

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
        bgfx::setTexture( 0, s_hdrColor, InTargets.PostInput );
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
