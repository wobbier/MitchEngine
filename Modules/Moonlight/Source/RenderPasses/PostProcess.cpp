#include "PostProcess.h"
#include "Camera/CameraData.h"
#include "Camera/DepthOfField.h"
#include "Device/FrameBuffer.h"
#include "Graphics/ColorLut.h"
#include "Graphics/Texture.h"
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
        m_outlineProgram = LoadPost( "Outline" );
        m_luminanceProgram = LoadPost( "Luminance" );
        m_adaptProgram = LoadPost( "Adapt" );
        m_fogScatterProgram = LoadPost( "FogScatter" );
        m_fogBlurProgram = LoadPost( "FogBlur" );
        m_fogProgram = LoadPost( "Fog" );
        m_taaProgram = LoadPost( "TAA" );
        m_copyProgram = LoadPost( "Copy" );
        m_dofCoCProgram = LoadPost( "DoFCoC" );
        m_dofPrefilterProgram = LoadPost( "DoFPrefilter" );
        m_dofDilateProgram = LoadPost( "DoFDilate" );
        m_dofGatherProgram = LoadPost( "DoFGather" );
        m_dofCompositeProgram = LoadPost( "DoFComposite" );

        s_dof0 = bgfx::createUniform( "s_dof0", bgfx::UniformType::Sampler );
        s_dof1 = bgfx::createUniform( "s_dof1", bgfx::UniformType::Sampler );
        s_dof2 = bgfx::createUniform( "s_dof2", bgfx::UniformType::Sampler );
        s_dof3 = bgfx::createUniform( "s_dof3", bgfx::UniformType::Sampler );
        s_dof4 = bgfx::createUniform( "s_dof4", bgfx::UniformType::Sampler );
        u_dofLens = bgfx::createUniform( "u_dofLens", bgfx::UniformType::Vec4 );
        u_dofSplit = bgfx::createUniform( "u_dofSplit", bgfx::UniformType::Vec4 );
        u_dofProjection = bgfx::createUniform( "u_dofProjection", bgfx::UniformType::Vec4 );
        u_dofScreen = bgfx::createUniform( "u_dofScreen", bgfx::UniformType::Vec4 );
        u_dofPass = bgfx::createUniform( "u_dofPass", bgfx::UniformType::Vec4 );
        u_dofJitter = bgfx::createUniform( "u_dofJitter", bgfx::UniformType::Vec4 );

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
        u_ssaoScreen = bgfx::createUniform( "u_ssaoScreen", bgfx::UniformType::Vec4 );
        u_ssaoInvProjection = bgfx::createUniform( "u_ssaoInvProjection", bgfx::UniformType::Mat4 );
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
        u_graphSceneFilter = bgfx::createUniform( "u_graphSceneFilter", bgfx::UniformType::Vec4 );
        s_lut = bgfx::createUniform( "s_lut", bgfx::UniformType::Sampler );
        s_lutStrip = bgfx::createUniform( "s_lutStrip", bgfx::UniformType::Sampler );
        u_lut = bgfx::createUniform( "u_lut", bgfx::UniformType::Vec4 );
        u_outline = bgfx::createUniform( "u_outline", bgfx::UniformType::Vec4 );
        u_outlineParams = bgfx::createUniform( "u_outlineParams", bgfx::UniformType::Vec4 );

        m_blackTexture = MakeSolid( 0.f );
        m_whiteTexture = MakeSolid( 1.f );
        const uint16_t volumeTexel[4] = {};
        m_identityVolume = bgfx::createTexture3D( 1, 1, 1, false, bgfx::TextureFormat::RGBA16F,
            BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP | BGFX_SAMPLER_W_CLAMP, bgfx::copy( volumeTexel, sizeof( volumeTexel ) ) );
    }


    PostProcess::~PostProcess()
    {
        for( bgfx::UniformHandle* uniform : { &s_input0, &s_input1, &s_hdrColor, &s_bloom, &s_exposure, &s_ldrColor, &u_tonemap, &u_grading, &u_grading2, &u_exposure, &u_bloom, &u_ssao, &u_ssaoProjection, &u_ssaoScreen, &u_ssaoInvProjection, &u_adapt,
            &u_fogProjection, &u_fogProjection2, &u_fogScreen, &u_fogInvView, &s_taaCurrent, &s_taaHistory, &s_taaDepth, &u_taaParams, &u_taaJitter, &u_taaReproject, &u_graphSceneFilter,
            &s_dof0, &s_dof1, &s_dof2, &s_dof3, &s_dof4, &u_dofLens, &u_dofSplit, &u_dofProjection, &u_dofScreen, &u_dofPass, &u_dofJitter, &s_lut, &s_lutStrip, &u_lut, &u_outline, &u_outlineParams } )
        {
            DestroyUniform( *uniform );
        }
        if( bgfx::isValid( m_blackTexture ) ) bgfx::destroy( m_blackTexture );
        if( bgfx::isValid( m_whiteTexture ) ) bgfx::destroy( m_whiteTexture );
        if( bgfx::isValid( m_identityVolume ) ) bgfx::destroy( m_identityVolume );
    }


    void PostProcess::SetupFullscreenView( bgfx::ViewId InView, bgfx::FrameBufferHandle InTarget, uint16_t InWidth, uint16_t InHeight, uint16_t InX, uint16_t InY )
    {
        float ortho[16];
        bx::mtxOrtho( ortho, 0.f, 1.f, 1.f, 0.f, 0.f, 100.f, 0.f, bgfx::getCaps()->homogeneousDepth );
        bgfx::setViewFrameBuffer( InView, InTarget );
        bgfx::setViewRect( InView, InX, InY, InWidth, InHeight );
        bgfx::setViewTransform( InView, nullptr, ortho );
        bgfx::touch( InView );
    }


    void PostProcess::SubmitFullscreen( bgfx::ViewId InView, const ShaderCommand& InProgram, uint64_t InState )
    {
        bgfx::setState( InState ? InState : kWriteColor );
        screenSpaceQuad( 1.f, 1.f, 0.f, bgfx::getCaps()->originBottomLeft );
        bgfx::submit( InView, InProgram.GetProgram() );
    }


    bgfx::TextureHandle PostProcess::RenderGraphSceneMips( ViewAllocator& InViews, FrameBuffer& InTargets, bgfx::TextureHandle InInput, float InThreshold )
    {
        InTargets.EnsureGraphSceneMips();
        const bgfx::ViewId view = InViews.Allocate( "Shader Graph Scene Mips" );
        if( view == UINT16_MAX ) return InInput;
        const FrameBuffer::Mip& target = InTargets.GraphSceneMips;
        SetupFullscreenView( view, target.Buffer, target.Width, target.Height );
        bgfx::setTexture( 0, s_input0, InInput, BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP );
        const float filter[4] = { InThreshold, 0.f, 0.f, 0.f };
        bgfx::setUniform( u_graphSceneFilter, filter );
        SubmitFullscreen( view, m_copyProgram );
        return target.Color;
    }


    void PostProcess::RenderAmbientOcclusion( ViewAllocator& InViews, const CameraData& InCamera, FrameBuffer& InTargets )
    {
        const PostProcessSettings& settings = InCamera.Post;
        if( !settings.AmbientOcclusion || settings.AOIntensity <= 0.f || settings.AORadius <= 0.f || !bgfx::isValid( InTargets.AOBuffer.Buffer ) || !bgfx::isValid( InTargets.AOBlurBuffer.Buffer ) || !bgfx::isValid( InTargets.DepthTexture ) || !bgfx::isValid( InTargets.ParticleBuffer ) )
        {
            return;
        }
        OPTICK_EVENT( "PostProcess::SSAO" );
        const glm::mat4& projection = InCamera.ProjectionMatrix.GetInternalMatrix();
        const glm::mat4 invProjection = glm::inverse( projection );

        const bgfx::ViewId aoView = InViews.Allocate( "SSAO" );
        const bgfx::ViewId blurView = InViews.Allocate( "SSAO Blur" );
        const bgfx::ViewId applyView = InViews.Allocate( "SSAO Apply" );
        if( applyView == UINT16_MAX )
        {
            return;
        }

        const float ssao[4] = { settings.AORadius, settings.AOIntensity, 0.05f, bgfx::getCaps()->originBottomLeft ? 1.f : -1.f };
        const float ssaoProjection[4] = { projection[0][0], projection[1][1], InCamera.Projection == ProjectionType::Orthographic ? 1.f : 0.f, bgfx::getCaps()->homogeneousDepth ? 1.f : 0.f };
        const float ssaoScreen[4] = { 1.f / std::max<float>( InTargets.Width, 1.f ), 1.f / std::max<float>( InTargets.Height, 1.f ), static_cast<float>( InTargets.AOBuffer.Width ), static_cast<float>( InTargets.AOBuffer.Height ) };
        const uint64_t pointClamp = BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
        // bgfx discards draw state on submit: the filter and upsample need these uniforms too.
        auto setAOState = [&]() {
            bgfx::setUniform( u_ssao, ssao );
            bgfx::setUniform( u_ssaoProjection, ssaoProjection );
            bgfx::setUniform( u_ssaoScreen, ssaoScreen );
            bgfx::setUniform( u_ssaoInvProjection, glm::value_ptr( invProjection ) );
        };

        SetupFullscreenView( aoView, InTargets.AOBuffer.Buffer, InTargets.AOBuffer.Width, InTargets.AOBuffer.Height );
        setAOState();
        bgfx::setTexture( 0, s_input0, InTargets.DepthTexture, pointClamp );
        SubmitFullscreen( aoView, m_ssaoProgram );

        SetupFullscreenView( blurView, InTargets.AOBlurBuffer.Buffer, InTargets.AOBlurBuffer.Width, InTargets.AOBlurBuffer.Height );
        setAOState();
        bgfx::setTexture( 0, s_input0, InTargets.AOBuffer.Color, pointClamp );
        bgfx::setTexture( 1, s_input1, InTargets.DepthTexture, pointClamp );
        SubmitFullscreen( blurView, m_ssaoBlurProgram );

        // Multiply the opaque HDR colour by AO (dst * src).
        // Use the colour-only framebuffer: the apply pass now samples depth for upsampling.
        SetupFullscreenView( applyView, InTargets.ParticleBuffer, static_cast<uint16_t>( InTargets.Width ), static_cast<uint16_t>( InTargets.Height ) );
        setAOState();
        bgfx::setTexture( 0, s_input0, InTargets.AOBlurBuffer.Color, pointClamp );
        bgfx::setTexture( 1, s_input1, InTargets.DepthTexture, pointClamp );
        SubmitFullscreen( applyView, m_ssaoApplyProgram, BGFX_STATE_WRITE_RGB | BGFX_STATE_BLEND_FUNC( BGFX_STATE_BLEND_DST_COLOR, BGFX_STATE_BLEND_ZERO ) );
    }


    void PostProcess::RenderOutlineEdges( ViewAllocator& InViews, const CameraData& InCamera, FrameBuffer& InTargets )
    {
        const PostProcessSettings& settings = InCamera.Post;
        if( !settings.Outline || !bgfx::isValid( InTargets.DepthTexture ) || !bgfx::isValid( InTargets.ParticleBuffer ) )
        {
            return;
        }
        OPTICK_EVENT( "PostProcess::OutlineEdges" );
        const bgfx::ViewId view = InViews.Allocate( "Outline Edges" );
        if( view == UINT16_MAX )
        {
            return;
        }
        const glm::mat4& projection = InCamera.ProjectionMatrix.GetInternalMatrix();
        const glm::mat4 invProjection = glm::inverse( projection );
        // The SSAO uniforms carry the depth-linearization state SSAOCommon.sh expects.
        const float ssao[4] = { 0.f, 0.f, 0.f, bgfx::getCaps()->originBottomLeft ? 1.f : -1.f };
        const float ssaoProjection[4] = { projection[0][0], projection[1][1], InCamera.Projection == ProjectionType::Orthographic ? 1.f : 0.f, bgfx::getCaps()->homogeneousDepth ? 1.f : 0.f };
        const float ssaoScreen[4] = { 1.f / std::max<float>( InTargets.Width, 1.f ), 1.f / std::max<float>( InTargets.Height, 1.f ), static_cast<float>( InTargets.Width ), static_cast<float>( InTargets.Height ) };
        const float color[4] = { SrgbToLinear( settings.OutlineColor[0] ), SrgbToLinear( settings.OutlineColor[1] ), SrgbToLinear( settings.OutlineColor[2] ), 1.f };
        const float params[4] = { std::max( settings.OutlineThickness, 0.5f ), std::max( settings.OutlineDepthThreshold, 0.001f ), std::clamp( settings.OutlineNormalThreshold, 0.01f, 2.f ), 0.f };
        const uint64_t pointClamp = BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;

        SetupFullscreenView( view, InTargets.ParticleBuffer, static_cast<uint16_t>( InTargets.Width ), static_cast<uint16_t>( InTargets.Height ) );
        bgfx::setUniform( u_ssao, ssao );
        bgfx::setUniform( u_ssaoProjection, ssaoProjection );
        bgfx::setUniform( u_ssaoScreen, ssaoScreen );
        bgfx::setUniform( u_ssaoInvProjection, glm::value_ptr( invProjection ) );
        bgfx::setUniform( u_outline, color );
        bgfx::setUniform( u_outlineParams, params );
        bgfx::setTexture( 0, s_input0, InTargets.DepthTexture, pointClamp );
        SubmitFullscreen( view, m_outlineProgram, BGFX_STATE_WRITE_RGB | BGFX_STATE_BLEND_ALPHA );
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
        // Rotate the march samples when temporal AA can accumulate them. A fixed screen-space
        // pattern survives the resolve as diagonal bands around bright, small local lights.
        const float fogJitter = InCamera.Post.TemporalAA ? static_cast<float>( InTargets.TemporalFrame % 32 ) * 0.61803398875f : 0.f;
        const float fogProjection2[4] = { projection[0][0], projection[1][1], InCamera.Far, fogJitter };
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
            bgfx::setTexture( 0, s_input0, InTargets.FogBuffer.Color, pointClamp );
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
        bgfx::setTexture( 0, s_input0, volumetric ? InTargets.FogBlurBuffer.Color : m_blackTexture, pointClamp );
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


    void PostProcess::RenderDepthOfField( ViewAllocator& InViews, const CameraData& InCamera, FrameBuffer& InTargets )
    {
        const PostProcessSettings& settings = InCamera.Post;
        if( !settings.DepthOfField || settings.DoFMaxBlurRadius <= 0.f || InCamera.Projection != ProjectionType::Perspective || !bgfx::isValid( InTargets.DepthTexture ) ) return;
        for( const ShaderCommand* program : { &m_dofCoCProgram, &m_dofPrefilterProgram, &m_dofDilateProgram, &m_dofGatherProgram, &m_dofCompositeProgram } )
        {
            if( !program->IsLoaded() ) return;
        }
        OPTICK_EVENT( "PostProcess::DepthOfField" );
        // Reserve the whole chain before submitting anything; an exhausted view budget must not
        // expose an unwritten target to bloom or tonemapping.
        const int lensCount = settings.DoFSplitDiopter ? 2 : 1;
        bgfx::ViewId views[14];
        const char* names[] = { "DoF CoC", "DoF Prefilter", "DoF Near Radius H", "DoF Near Radius V", "DoF Far Gather", "DoF Near Gather", "DoF Composite" };
        for( int i = 0; i < 7 * lensCount; ++i )
        {
            views[i] = InViews.Allocate( names[i % 7] );
            if( views[i] == UINT16_MAX ) return;
        }
        InTargets.EnsureDepthOfFieldTargets( settings.DoFSplitDiopter );
        const glm::mat4& projection = InCamera.ProjectionMatrix.GetInternalMatrix();
        const float projectionParams[4] = { projection[2][2], projection[3][2], bgfx::getCaps()->homogeneousDepth ? 1.f : 0.f, InCamera.Far };
        const float screen[4] = { static_cast<float>( InTargets.Width ), static_cast<float>( InTargets.Height ), static_cast<float>( InTargets.DoFPrefilter.Width ), static_cast<float>( InTargets.DoFPrefilter.Height ) };
        // TAA returns an unjittered colour image; sample its corresponding current-frame depth.
        const float jitter[4] = { InCamera.Post.TemporalAA ? InTargets.TemporalJitter[0] * 0.5f : 0.f, InCamera.Post.TemporalAA ? -InTargets.TemporalJitter[1] * 0.5f : 0.f, 0.f, 0.f };
        const uint64_t clamp = BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
        const uint64_t point = clamp | BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT;
        // Each lens must gather with a spatially consistent CoC/depth ordering. Reuse the
        // scratch targets in view order, preserving the first completed image for composition.
        for( int lensIndex = 0; lensIndex < lensCount; ++lensIndex )
        {
            PostProcessSettings lensSettings = settings;
            if( lensIndex == 1 ) lensSettings.DoFFocusDistance = settings.DoFSplitFocusDistance;
            const DepthOfFieldLens lens = MakeDepthOfFieldLens( lensSettings, projection[1][1], InTargets.Height );
            const float lensParams[4] = { lens.FocusDistance, lens.Scale, lens.MaxRadius, settings.DoFDebugCoC ? 1.f : 0.f };
            const float splitParams[4] = { std::clamp( settings.DoFSplitPosition, 0.f, 1.f ),
                std::clamp( settings.DoFSplitFeather, 0.f, 0.25f ), static_cast<float>( lensIndex ), 0.f };
            const bgfx::ViewId* lensViews = views + lensIndex * 7;
            auto begin = [&]( int InIndex, const FrameBuffer::Mip& InTarget, float InPass ) {
                SetupFullscreenView( lensViews[InIndex], InTarget.Buffer, InTarget.Width, InTarget.Height );
                const float pass[4] = { InPass, 0.f, 0.f, 0.f };
                bgfx::setUniform( u_dofLens, lensParams );
                bgfx::setUniform( u_dofSplit, splitParams );
                bgfx::setUniform( u_dofProjection, projectionParams );
                bgfx::setUniform( u_dofScreen, screen );
                bgfx::setUniform( u_dofPass, pass );
                bgfx::setUniform( u_dofJitter, jitter );
            };
            begin( 0, InTargets.DoFCoC, 0.f );
            bgfx::setTexture( 0, s_dof0, InTargets.DepthTexture, point );
            SubmitFullscreen( lensViews[0], m_dofCoCProgram );

            begin( 1, InTargets.DoFPrefilter, 0.f );
            bgfx::setTexture( 0, s_dof0, InTargets.PostInput, point );
            bgfx::setTexture( 1, s_dof1, InTargets.DoFCoC.Color, point );
            SubmitFullscreen( lensViews[1], m_dofPrefilterProgram );

            for( int axis = 0; axis < 2; ++axis )
            {
                begin( 2 + axis, InTargets.DoFRadius[axis], static_cast<float>( axis ) );
                bgfx::setTexture( 0, s_dof0, axis == 0 ? InTargets.DoFPrefilter.Color : InTargets.DoFRadius[0].Color, point );
                SubmitFullscreen( lensViews[2 + axis], m_dofDilateProgram );
            }
            for( int near = 0; near < 2; ++near )
            {
                begin( 4 + near, near ? InTargets.DoFNear : InTargets.DoFFar, static_cast<float>( near ) );
                bgfx::setTexture( 0, s_dof0, InTargets.DoFPrefilter.Color, point );
                bgfx::setTexture( 1, s_dof1, InTargets.DoFRadius[1].Color, point );
                SubmitFullscreen( lensViews[4 + near], m_dofGatherProgram );
            }
            begin( 6, lensIndex == 0 ? InTargets.DoFOutput : InTargets.DoFSplitOutput, 0.f );
            bgfx::setTexture( 0, s_dof0, InTargets.PostInput, clamp );
            bgfx::setTexture( 1, s_dof1, InTargets.DoFCoC.Color, point );
            bgfx::setTexture( 2, s_dof2, InTargets.DoFFar.Color, clamp );
            bgfx::setTexture( 3, s_dof3, InTargets.DoFNear.Color, clamp );
            bgfx::setTexture( 4, s_dof4, lensIndex == 0 ? InTargets.PostInput : InTargets.DoFOutput.Color, clamp );
            SubmitFullscreen( lensViews[6], m_dofCompositeProgram );
        }
        InTargets.PostInput = settings.DoFSplitDiopter ? InTargets.DoFSplitOutput.Color : InTargets.DoFOutput.Color;
    }


    void PostProcess::Render( ViewAllocator& InViews, const CameraData& InCamera, FrameBuffer& InTargets, bgfx::FrameBufferHandle InOutput, uint16_t InOutputWidth, uint16_t InOutputHeight, uint16_t InOutputX, uint16_t InOutputY )
    {
        OPTICK_EVENT( "PostProcess::Render" );
        const PostProcessSettings& settings = InCamera.Post;
        if( !bgfx::isValid( InTargets.PostInput ) )
        {
            InTargets.PostInput = InTargets.SceneColor;
        }

        RenderDepthOfField( InViews, InCamera, InTargets );
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
            SetupFullscreenView( tonemapView, InOutput, InOutputWidth, InOutputHeight, InOutputX, InOutputY );
        }

        const float compensation = std::pow( 2.f, settings.ExposureCompensation );
        const float tonemap[4] = { compensation, static_cast<float>( settings.Operator ), bloom ? settings.BloomIntensity : 0.f, settings.VignetteIntensity };
        const float grading[4] = { settings.Saturation, settings.Contrast, settings.Temperature, settings.Tint };
        const float grading2[4] = { settings.VignetteSmoothness, settings.Gamma, settings.Lift, settings.Gain };
        const float exposure[4] = { settings.AutoExposure ? 1.f : 0.f, compensation, fxaa ? 1.f : 0.f, 0.f };
        // The grading LUT: a .cube volume, else an image strip of N slices of NxN (height N,
        // width NxN; anything else is ignored rather than sampled wrongly).
        float lutMode = 0.f;
        float lutSize = 0.f;
        bgfx::TextureHandle lutVolume = m_identityVolume;
        bgfx::TextureHandle lutStrip = m_blackTexture;
        const float lutContribution = std::clamp( settings.LutContribution, 0.f, 1.f );
        if( lutContribution > 0.f )
        {
            if( settings.Lut && settings.Lut->IsReady() )
            {
                lutMode = 1.f;
                lutSize = static_cast<float>( settings.Lut->GetSize() );
                lutVolume = settings.Lut->GetHandle();
            }
            else if( settings.LutStrip && bgfx::isValid( settings.LutStrip->TexHandle )
                && settings.LutStrip->mHeight > 1 && settings.LutStrip->mWidth == settings.LutStrip->mHeight * settings.LutStrip->mHeight )
            {
                lutMode = 2.f;
                lutSize = static_cast<float>( settings.LutStrip->mHeight );
                lutStrip = settings.LutStrip->TexHandle;
            }
        }
        const float lut[4] = { lutSize, lutContribution, lutMode, 0.f };
        bgfx::setUniform( u_tonemap, tonemap );
        bgfx::setUniform( u_grading, grading );
        bgfx::setUniform( u_grading2, grading2 );
        bgfx::setUniform( u_exposure, exposure );
        bgfx::setUniform( u_lut, lut );
        bgfx::setTexture( 0, s_hdrColor, InTargets.PostInput );
        bgfx::setTexture( 1, s_bloom, bloom ? InTargets.BloomMips[0].Color : m_blackTexture );
        bgfx::setTexture( 2, s_exposure, exposureTexture );
        bgfx::setTexture( 3, s_lut, lutVolume, BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP | BGFX_SAMPLER_W_CLAMP );
        bgfx::setTexture( 4, s_lutStrip, lutStrip, BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP );
        SubmitFullscreen( tonemapView, m_tonemapProgram );

        if( fxaa )
        {
            const bgfx::ViewId fxaaView = InViews.Allocate( "FXAA" );
            if( fxaaView == UINT16_MAX )
            {
                return;
            }
            SetupFullscreenView( fxaaView, InOutput, InOutputWidth, InOutputHeight, InOutputX, InOutputY );
            bgfx::setTexture( 0, s_ldrColor, InTargets.PostColor );
            SubmitFullscreen( fxaaView, m_fxaaProgram );
        }
    }
}
