#include "PostProcess.h"
#include "Camera/CameraData.h"
#include "Camera/DepthOfField.h"
#include "Device/FrameBuffer.h"
#include "Graphics/ColorLut.h"
#include "Graphics/Texture.h"
#include "Primitives/Cube.h"
#include "RenderViews.h"
#include "TextureHistogram.h"
#include "Core/CommandLine.h"
#include <CLog.h>
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
        OutVolume[3] = volumetric ? static_cast<float>( std::min( InSettings.VolumetricSteps, PostProcess::kMaxVolumetricSteps ) ) : 0.f;
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
        m_histogramMeteringDisabled = CommandLine::Has( "--no-histogram-exposure" );
        m_luminanceProgram = LoadPost( "Luminance" );
        m_adaptProgram = LoadPost( "Adapt" );
        m_fogScatterProgram = LoadPost( "FogScatter" );
        m_fogScatterLightsProgram = LoadPost( "FogScatterLights" );
        m_fogBlurProgram = LoadPost( "FogBlur" );
        m_fogResolveProgram = LoadPost( "FogResolve" );
        m_fogProgram = LoadPost( "Fog" );
        m_fogFroxelProgram = LoadPost( "FogFroxel" );
        FroxelGridRows = std::clamp( CommandLine::GetInt( "--froxel-grid-rows", kFroxelGridRows ), 16, 256 );
        FroxelSlices = std::clamp( CommandLine::GetInt( "--froxel-slices", 0 ), 0, kFroxelMaxDepth );
        FroxelNearDepth = std::max( CommandLine::GetFloat( "--froxel-near", kFroxelNearDepth ), 0.05f );
        m_taaProgram = LoadPost( "TAA" );
        m_copyProgram = LoadPost( "Copy" );
        m_upscaleProgram = LoadPost( "Upscale" );
        m_mirrorProgram = LoadPost( "Mirror" );
        m_overlayProgram = LoadPost( "OverlayComposite" );
        m_eraProgram = LoadPost( "Era" );
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
        u_upscaleParams = bgfx::createUniform( "u_upscaleParams", bgfx::UniformType::Vec4 );
        u_upscaleSize = bgfx::createUniform( "u_upscaleSize", bgfx::UniformType::Vec4 );
        u_mirrorRect = bgfx::createUniform( "u_mirrorRect", bgfx::UniformType::Vec4 );
        u_mirrorTaps = bgfx::createUniform( "u_mirrorTaps", bgfx::UniformType::Vec4 );
        u_overlayParams = bgfx::createUniform( "u_overlayParams", bgfx::UniformType::Vec4 );
        u_overlayFade = bgfx::createUniform( "u_overlayFade", bgfx::UniformType::Vec4 );
        u_overlayLook = bgfx::createUniform( "u_overlayLook", bgfx::UniformType::Vec4 );
        u_eraPost = bgfx::createUniform( "u_eraPost", bgfx::UniformType::Vec4 );
        s_input1 = bgfx::createUniform( "s_input1", bgfx::UniformType::Sampler );
        s_hdrColor = bgfx::createUniform( "s_hdrColor", bgfx::UniformType::Sampler );
        s_bloom = bgfx::createUniform( "s_bloom", bgfx::UniformType::Sampler );
        s_exposure = bgfx::createUniform( "s_exposure", bgfx::UniformType::Sampler );
        s_ldrColor = bgfx::createUniform( "s_ldrColor", bgfx::UniformType::Sampler );
        u_tonemap = bgfx::createUniform( "u_tonemap", bgfx::UniformType::Vec4 );
        u_grading = bgfx::createUniform( "u_grading", bgfx::UniformType::Vec4 );
        u_grading2 = bgfx::createUniform( "u_grading2", bgfx::UniformType::Vec4 );
        u_grading3 = bgfx::createUniform( "u_grading3", bgfx::UniformType::Vec4 );
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
        u_fogVolume2 = bgfx::createUniform( "u_fogVolume2", bgfx::UniformType::Vec4 );
        u_fogScatter = bgfx::createUniform( "u_fogScatter", bgfx::UniformType::Vec4 );
        u_fogReproject = bgfx::createUniform( "u_fogReproject", bgfx::UniformType::Mat4 );
        s_fogHistory = bgfx::createUniform( "s_fogHistory", bgfx::UniformType::Sampler );
        u_froxelGrid = bgfx::createUniform( "u_froxelGrid", bgfx::UniformType::Vec4 );
        u_froxelDepth = bgfx::createUniform( "u_froxelDepth", bgfx::UniformType::Vec4 );
        u_froxelParams = bgfx::createUniform( "u_froxelParams", bgfx::UniformType::Vec4 );
        u_froxelPrevView = bgfx::createUniform( "u_froxelPrevView", bgfx::UniformType::Mat4 );
        s_froxelRaw = bgfx::createUniform( "s_froxelRaw", bgfx::UniformType::Sampler );
        s_froxelHistory = bgfx::createUniform( "s_froxelHistory", bgfx::UniformType::Sampler );
        s_froxelIn = bgfx::createUniform( "s_froxelIn", bgfx::UniformType::Sampler );
        s_froxelVolume = bgfx::createUniform( "s_froxelVolume", bgfx::UniformType::Sampler );
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
        u_grain = bgfx::createUniform( "u_grain", bgfx::UniformType::Vec4 );
        u_lens = bgfx::createUniform( "u_lens", bgfx::UniformType::Vec4 );
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
        for( bgfx::UniformHandle* uniform : { &s_input0, &s_input1, &s_hdrColor, &s_bloom, &s_exposure, &s_ldrColor, &u_tonemap, &u_grading, &u_grading2, &u_grading3, &u_exposure, &u_bloom, &u_ssao, &u_ssaoProjection, &u_ssaoScreen, &u_ssaoInvProjection, &u_adapt,
            &u_fogProjection, &u_fogProjection2, &u_fogScreen, &u_fogInvView, &u_fogVolume2, &u_fogScatter, &u_fogReproject, &s_fogHistory,
            &u_froxelGrid, &u_froxelDepth, &u_froxelParams, &u_froxelPrevView, &s_froxelRaw, &s_froxelHistory, &s_froxelIn, &s_froxelVolume, &s_taaCurrent, &s_taaHistory, &s_taaDepth, &u_taaParams, &u_taaJitter, &u_taaReproject, &u_graphSceneFilter,
            &s_dof0, &s_dof1, &s_dof2, &s_dof3, &s_dof4, &u_dofLens, &u_dofSplit, &u_dofProjection, &u_dofScreen, &u_dofPass, &u_dofJitter, &s_lut, &s_lutStrip, &u_lut, &u_outline, &u_outlineParams, &u_upscaleParams, &u_upscaleSize, &u_mirrorRect, &u_mirrorTaps, &u_overlayParams, &u_overlayFade, &u_overlayLook, &u_eraPost, &u_grain, &u_lens } )
        {
            DestroyUniform( *uniform );
        }
        if( bgfx::isValid( m_meteredLuminance ) ) bgfx::destroy( m_meteredLuminance );
        DestroyUniform( u_meterParams );
        if( bgfx::isValid( m_blackTexture ) ) bgfx::destroy( m_blackTexture );
        if( bgfx::isValid( m_whiteTexture ) ) bgfx::destroy( m_whiteTexture );
        if( bgfx::isValid( m_identityVolume ) ) bgfx::destroy( m_identityVolume );
    }


    void PostProcess::SetupFullscreenView( bgfx::ViewId InView, bgfx::FrameBufferHandle InTarget, uint16_t InWidth, uint16_t InHeight, uint16_t InX, uint16_t InY )
    {
        // Writing a render texture on a bottom-up backend: flipped, so it holds its top row first.
        const bool flip = bgfx::isValid( InTarget ) && InTarget.idx == FlippedTarget.idx;
        float ortho[16];
        bx::mtxOrtho( ortho, 0.f, 1.f, flip ? 0.f : 1.f, flip ? 1.f : 0.f, 0.f, 100.f, 0.f, bgfx::getCaps()->homogeneousDepth );
        bgfx::setViewFrameBuffer( InView, FrameBuffer::GuardTarget( InTarget ) );
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
        if( !settings.AmbientOcclusion || settings.AOIntensity <= 0.f || settings.AORadius <= 0.f )
        {
            return;
        }
        InTargets.EnsureAmbientOcclusionTargets();
        if( !bgfx::isValid( InTargets.AOBuffer.Buffer ) || !bgfx::isValid( InTargets.AOBlurBuffer.Buffer ) || !bgfx::isValid( InTargets.DepthTexture ) || !bgfx::isValid( InTargets.ParticleBuffer ) )
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


    void PostProcess::RenderFog( ViewAllocator& InViews, const CameraData& InCamera, FrameBuffer& InTargets, const std::function<void()>& InBindLighting, bool InFroxelFog )
    {
        const PostProcessSettings& settings = InCamera.Post;
        if( !settings.Fog || settings.FogDensity <= 0.f || settings.FogMaxOpacity <= 0.f || !bgfx::isValid( InTargets.ParticleBuffer ) )
        {
            InTargets.FogHistoryValid = false;
            InTargets.FogFrame = 0;
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
        // The froxel path (compute): its programs and volumes are made on its first frame, and the
        // march stands in while a program loads or where 3D image writes aren't supported.
        bool froxel = false;
        uint16_t froxelSize[3] = { 0, 0, 0 };
        if( volumetric && InFroxelFog && m_fogFroxelProgram.IsLoaded() )
        {
            if( !m_froxelInjectProgram )
            {
                m_froxelInjectProgram = AcquireComputeProgram( "Assets/Shaders/Post/FogFroxelInject.comp" );
                m_froxelBlendProgram = AcquireComputeProgram( "Assets/Shaders/Post/FogFroxelBlend.comp" );
                m_froxelIntegrateProgram = AcquireComputeProgram( "Assets/Shaders/Post/FogFroxelIntegrate.comp" );
            }
            froxel = m_froxelInjectProgram && bgfx::isValid( m_froxelInjectProgram->Handle ) && m_froxelBlendProgram && bgfx::isValid( m_froxelBlendProgram->Handle )
                && m_froxelIntegrateProgram && bgfx::isValid( m_froxelIntegrateProgram->Handle );
            if( froxel )
            {
                // Square cells of a fixed angle (rows), as many columns as the aspect needs; the
                // slices follow VolumetricSteps (which the hardware profile's MaxVolumetricSteps
                // has already capped).
                const float aspect = static_cast<float>( std::max<uint32_t>( InTargets.Width, 1 ) ) / static_cast<float>( std::max<uint32_t>( InTargets.Height, 1 ) );
                froxelSize[0] = static_cast<uint16_t>( std::clamp( static_cast<int>( std::lround( FroxelGridRows * aspect ) ), 16, kFroxelMaxColumns ) );
                froxelSize[1] = static_cast<uint16_t>( FroxelGridRows );
                froxelSize[2] = static_cast<uint16_t>( std::clamp( FroxelSlices > 0 ? FroxelSlices : settings.VolumetricSteps, kFroxelMinDepth, kFroxelMaxDepth ) );
                froxel = InTargets.EnsureFroxelTargets( froxelSize[0], froxelSize[1], froxelSize[2] );
            }
        }
        // Each path keeps its own history: switching drops it.
        if( InTargets.FogHistoryFroxel != froxel )
        {
            InTargets.FogHistoryValid = false;
            InTargets.FogFrame = 0;
            InTargets.FogHistoryFroxel = froxel;
        }
        if( volumetric && !froxel )
        {
            // Created on first use: cameras with analytic fog never allocate them.
            InTargets.EnsureVolumetricFogTargets();
            if( !bgfx::isValid( InTargets.FogBuffer.Buffer ) || !bgfx::isValid( InTargets.FogBlurBuffer.Buffer ) )
            {
                InTargets.FogHistoryValid = false;
                InTargets.FogFrame = 0;
                return;
            }
        }
        const bool temporal = volumetric && settings.VolumetricTemporal;
        if( !temporal )
        {
            InTargets.FogHistoryValid = false;
            InTargets.FogFrame = 0;
        }
        const float fogProjection[4] = { projection[2][2], projection[3][2], InCamera.Projection == ProjectionType::Orthographic ? 1.f : 0.f, bgfx::getCaps()->homogeneousDepth ? 1.f : 0.f };
        // Rotate the march samples when something accumulates them: the fog's own history, or
        // temporal AA. A fixed screen-space pattern survives a resolve as diagonal bands around
        // bright, small local lights.
        const float fogJitter = temporal ? static_cast<float>( InTargets.FogFrame % 32 ) * 0.61803398875f
            : ( InCamera.Post.TemporalAA ? static_cast<float>( InTargets.TemporalFrame % 32 ) * 0.61803398875f : 0.f );
        const float fogProjection2[4] = { projection[0][0], projection[1][1], InCamera.Far, fogJitter };
        // The volumetric targets' size (half resolution; analytic fog never allocates them).
        const float fogScreen[4] = { static_cast<float>( InTargets.Width ), static_cast<float>( InTargets.Height ), static_cast<float>( std::max<uint32_t>( InTargets.Width / 2, 1 ) ), static_cast<float>( std::max<uint32_t>( InTargets.Height / 2, 1 ) ) };
        const uint64_t pointClamp = BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
        const uint64_t linearClamp = BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;

        // The history is reprojected through the opaque depth: unjittered clip space now -> last
        // frame's. A cut (the camera jumping or turning away) drops it rather than smearing.
        glm::mat4 reproject( 1.f );
        // The froxel history is reprojected through last frame's view and projection scale.
        glm::mat4 froxelPreviousView( 1.f );
        float froxelPreviousProjection[2] = { projection[0][0], projection[1][1] };
        if( temporal )
        {
            if( froxel )
            {
                froxelPreviousView = glm::make_mat4( InTargets.FogPreviousView );
                froxelPreviousProjection[0] = InTargets.FogPreviousProjection[0];
                froxelPreviousProjection[1] = InTargets.FogPreviousProjection[1];
                const glm::mat4& view = InCamera.View.GetInternalMatrix();
                std::copy( &view[0][0], &view[0][0] + 16, InTargets.FogPreviousView );
                InTargets.FogPreviousProjection[0] = projection[0][0];
                InTargets.FogPreviousProjection[1] = projection[1][1];
            }
            else
            {
                InTargets.EnsureFogHistoryTargets();
            }
            const glm::vec3 origin( invView[3] );
            const glm::vec3 forward = glm::normalize( glm::vec3( invView[2] ) );
            if( InTargets.FogHistoryValid )
            {
                const glm::vec3 previousOrigin = glm::make_vec3( InTargets.FogPreviousOrigin );
                const glm::vec3 previousForward = glm::make_vec3( InTargets.FogPreviousForward );
                if( glm::distance( origin, previousOrigin ) > 5.f || glm::dot( forward, previousForward ) < 0.9f )
                {
                    InTargets.FogHistoryValid = false;
                }
            }
            if( InTargets.FogHistoryValid )
            {
                reproject = glm::make_mat4( InTargets.FogPreviousViewProjection ) * glm::inverse( glm::make_mat4( InTargets.ViewProjection ) );
            }
            std::copy( InTargets.ViewProjection, InTargets.ViewProjection + 16, InTargets.FogPreviousViewProjection );
            std::copy( &origin[0], &origin[0] + 3, InTargets.FogPreviousOrigin );
            std::copy( &forward[0], &forward[0] + 3, InTargets.FogPreviousForward );
        }
        // x local-light anisotropy, y history weight, z 1 = history valid, w blur radius (texels):
        // the 5x5 blur hides a single frame's jitter; once the history has converged 3x3 keeps
        // the beams crisper.
        const float fogVolume2[4] = { std::clamp( settings.FogLocalAnisotropy, 0.f, 0.95f ), kFogHistoryWeight, InTargets.FogHistoryValid ? 1.f : 0.f, temporal && InTargets.FogHistoryValid ? 1.f : 2.f };
        // VolumetricLightSamples swaps the scatter pass for the one that integrates each local
        // light on its own stretch of the ray (x: samples per light); without it the march lights
        // every step from its cluster, in the shader it always had.
        const int lightSamples = std::clamp( settings.VolumetricLightSamples, 0, kMaxVolumetricLightSamples );
        // y: how many of the local lights the fog shows (they come first in the light data, ME-409).
        const float fogScatter[4] = { static_cast<float>( lightSamples ), static_cast<float>( FogLocalLightCount ), 0.f, 0.f };
        auto setFogState = [&]() {
            InBindLighting();
            bgfx::setUniform( u_fogProjection, fogProjection );
            bgfx::setUniform( u_fogProjection2, fogProjection2 );
            bgfx::setUniform( u_fogScreen, fogScreen );
            bgfx::setUniform( u_fogInvView, &invView[0][0] );
            bgfx::setUniform( u_fogVolume2, fogVolume2 );
            bgfx::setUniform( u_fogScatter, fogScatter );
            bgfx::setUniform( u_fogReproject, &reproject[0][0] );
        };

        // Froxel grid: exponential slices from gridNear to the march distance (in view depth); fog
        // from its start up to gridNear is lit with the first slice, fog beyond gridFar is analytic.
        const float fogStart = fogParams[3];
        const float gridNear = std::max( { InCamera.Near, fogStart, FroxelNearDepth } );
        const float gridFar = std::max( fogVolume[2], gridNear + 1.f );
        const float froxelGrid[4] = { static_cast<float>( froxelSize[0] ), static_cast<float>( froxelSize[1] ), static_cast<float>( froxelSize[2] ), InTargets.FogHistoryValid ? 1.f : 0.f };
        // w: the frame's place in the cells' jitter sequence (-1: cell centres, nothing accumulates them).
        const float froxelDepth[4] = { gridNear, gridFar, 1.f / std::log( gridFar / gridNear ), temporal ? static_cast<float>( InTargets.FogFrame % 1024 ) : -1.f };
        const float froxelParams[4] = { kFogHistoryWeight, std::min( fogStart, gridNear ), froxelPreviousProjection[0], froxelPreviousProjection[1] };
        auto setFroxelState = [&]() {
            setFogState();
            bgfx::setUniform( u_froxelGrid, froxelGrid );
            bgfx::setUniform( u_froxelDepth, froxelDepth );
            bgfx::setUniform( u_froxelParams, froxelParams );
            bgfx::setUniform( u_froxelPrevView, &froxelPreviousView[0][0] );
        };
        const uint64_t linearClamp3D = BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP | BGFX_SAMPLER_W_CLAMP;

        if( froxel )
        {
            // One view per pass: each reads the previous pass's volume through a sampler, and bgfx
            // orders views, not dispatches within one. They follow both shadow passes and the
            // opaque view (the camera's list), and precede the apply draw.
            const bgfx::ViewId injectView = InViews.Allocate( "Fog Froxel Inject" );
            const bgfx::ViewId blendView = temporal ? InViews.Allocate( "Fog Froxel Blend" ) : injectView;
            const bgfx::ViewId integrateView = InViews.Allocate( "Fog Froxel Integrate" );
            if( integrateView == UINT16_MAX || blendView == UINT16_MAX )
            {
                InTargets.FogHistoryValid = false;
                return;
            }
            const uint32_t groupsX = ComputeGroupCount( froxelSize[0], 8 );
            const uint32_t groupsY = ComputeGroupCount( froxelSize[1], 8 );

            setFroxelState();
            bgfx::setImage( 0, InTargets.FroxelRaw, 0, bgfx::Access::Write, bgfx::TextureFormat::RGBA16F );
            bgfx::dispatch( injectView, m_froxelInjectProgram->Handle, groupsX, groupsY, froxelSize[2] );

            bgfx::TextureHandle integrateInput = InTargets.FroxelRaw;
            if( temporal )
            {
                const uint32_t read = InTargets.FroxelHistoryIndex;
                const uint32_t write = 1 - read;
                setFroxelState();
                bgfx::setImage( 0, InTargets.FroxelHistory[write], 0, bgfx::Access::Write, bgfx::TextureFormat::RGBA16F );
                bgfx::setTexture( 1, s_froxelRaw, InTargets.FroxelRaw, linearClamp3D );
                bgfx::setTexture( 2, s_froxelHistory, InTargets.FogHistoryValid ? InTargets.FroxelHistory[read] : m_identityVolume, linearClamp3D );
                bgfx::dispatch( blendView, m_froxelBlendProgram->Handle, groupsX, groupsY, froxelSize[2] );
                InTargets.FroxelHistoryIndex = write;
                InTargets.FogHistoryValid = true;
                ++InTargets.FogFrame;
                integrateInput = InTargets.FroxelHistory[write];
            }

            setFroxelState();
            bgfx::setImage( 0, InTargets.FroxelIntegrated, 0, bgfx::Access::Write, bgfx::TextureFormat::RGBA16F );
            bgfx::setTexture( 1, s_froxelIn, integrateInput, linearClamp3D );
            bgfx::dispatch( integrateView, m_froxelIntegrateProgram->Handle, groupsX, groupsY, 1 );
            if( !m_loggedFroxelFog )
            {
                m_loggedFroxelFog = true;
                CLog::Log( CLog::LogType::Info, "Froxel fog: first compute dispatch (" + std::to_string( froxelSize[0] ) + "x" + std::to_string( froxelSize[1] ) + "x" + std::to_string( froxelSize[2] ) + ")" );
            }
        }
        else if( volumetric )
        {
            const bgfx::ViewId scatterView = InViews.Allocate( "Fog Scatter" );
            const bgfx::ViewId resolveView = temporal ? InViews.Allocate( "Fog Resolve" ) : scatterView;
            const bgfx::ViewId blurView = InViews.Allocate( "Fog Blur" );
            if( blurView == UINT16_MAX || resolveView == UINT16_MAX )
            {
                InTargets.FogHistoryValid = false;
                return;
            }
            SetupFullscreenView( scatterView, InTargets.FogBuffer.Buffer, InTargets.FogBuffer.Width, InTargets.FogBuffer.Height );
            setFogState();
            bgfx::setTexture( 0, s_input0, InTargets.DepthTexture, pointClamp );
            // #TODO Direct3D: FogScatterLights intermittently writes Inf/NaN there (whole frames go
            // white once the history holds it), while Vulkan is fine. It follows the compiled
            // shader, not its inputs: the same passes replay clean in RenderDoc, and adding or
            // removing unrelated code (the cluster light loop) flips it. Until the cause is found,
            // Direct3D keeps the cluster march that stands in while this program loads.
            const bgfx::RendererType::Enum rendererType = bgfx::getRendererType();
            const bool perLightScatter = rendererType != bgfx::RendererType::Direct3D11 && rendererType != bgfx::RendererType::Direct3D12;
            SubmitFullscreen( scatterView, perLightScatter && lightSamples > 0 && m_fogScatterLightsProgram.IsLoaded() ? m_fogScatterLightsProgram : m_fogScatterProgram );

            bgfx::TextureHandle blurInput = InTargets.FogBuffer.Color;
            if( temporal )
            {
                const uint32_t read = InTargets.FogHistoryIndex;
                const uint32_t write = 1 - read;
                FrameBuffer::Mip& target = InTargets.FogHistory[write];
                SetupFullscreenView( resolveView, target.Buffer, target.Width, target.Height );
                setFogState();
                bgfx::setTexture( 0, s_input0, InTargets.FogBuffer.Color, pointClamp );
                bgfx::setTexture( 1, s_input1, InTargets.DepthTexture, pointClamp );
                bgfx::setTexture( 2, s_fogHistory, InTargets.FogHistoryValid ? InTargets.FogHistory[read].Color : m_blackTexture, linearClamp );
                SubmitFullscreen( resolveView, m_fogResolveProgram );
                InTargets.FogHistoryIndex = write;
                InTargets.FogHistoryValid = true;
                ++InTargets.FogFrame;
                blurInput = target.Color;
            }

            SetupFullscreenView( blurView, InTargets.FogBlurBuffer.Buffer, InTargets.FogBlurBuffer.Width, InTargets.FogBlurBuffer.Height );
            setFogState();
            bgfx::setTexture( 0, s_input0, blurInput, pointClamp );
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
        if( froxel )
        {
            setFroxelState();
            bgfx::setTexture( 0, s_froxelVolume, InTargets.FroxelIntegrated, linearClamp3D );
            bgfx::setTexture( 1, s_input1, InTargets.DepthTexture, pointClamp );
            SubmitFullscreen( applyView, m_fogFroxelProgram, BGFX_STATE_WRITE_RGB | BGFX_STATE_BLEND_FUNC( BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_SRC_ALPHA ) );
            return;
        }
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


    bool PostProcess::RenderHistogramLuminance( ViewAllocator& InViews, const PostProcessSettings& InSettings, FrameBuffer& InTargets, bgfx::TextureHandle& OutLuminance )
    {
        if( !m_histogram )
        {
            // Only where compute is active: a GL backend without compute aborts on a .comp that
            // fails to compile.
            m_histogram = MakeUnique<TextureHistogram>();
            m_exposureHistogramProgram = AcquireComputeProgram( "Assets/Shaders/Post/ExposureHistogram.comp" );
            m_meteredLuminance = bgfx::createTexture2D( 1, 1, false, 1, bgfx::TextureFormat::R32F, BGFX_TEXTURE_COMPUTE_WRITE | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP | BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT, nullptr );
            bgfx::setName( m_meteredLuminance, "Metered Luminance" );
            u_meterParams = bgfx::createUniform( "u_meterParams", bgfx::UniformType::Vec4 );
        }
        if( !m_histogram->IsReady() || !m_exposureHistogramProgram || !bgfx::isValid( m_exposureHistogramProgram->Handle ) || !bgfx::isValid( m_meteredLuminance ) || !bgfx::isValid( InTargets.PostInput ) )
        {
            return false;
        }
        const bgfx::ViewId binView = InViews.Allocate( "Luminance Histogram" );
        const bgfx::ViewId mergeView = InViews.Allocate( "Luminance Histogram Merge" );
        const bgfx::ViewId meterView = InViews.Allocate( "Metering" );
        if( meterView == UINT16_MAX )
        {
            return false;
        }
        // Log2 luminance over the adaptation range plus two stops each side (values outside land
        // in the end bins, and the adaptation clamps to the range anyway).
        const float minLuminance = std::pow( 2.f, InSettings.AutoExposureMinEV ) * 0.125f;
        const float maxLuminance = std::pow( 2.f, InSettings.AutoExposureMaxEV ) * 0.125f;
        TextureHistogram::Params params;
        params.Min = std::log2( minLuminance ) - 2.f;
        params.Max = std::max( std::log2( maxLuminance ) + 2.f, params.Min + 1.f );
        params.Logarithmic = true;
        if( !m_histogram->Dispatch( binView, mergeView, InTargets.PostInput, params ) )
        {
            return false;
        }

        const float low = std::clamp( InSettings.AutoExposureLowPercentile, 0.f, 1.f );
        const float high = std::clamp( InSettings.AutoExposureHighPercentile, low, 1.f );
        const float meter[4] = { low, high, std::sqrt( minLuminance * maxLuminance ), 0.f };
        bgfx::setUniform( u_meterParams, meter );
        m_histogram->SetRangeUniform( params );
        bgfx::setBuffer( 0, m_histogram->GetMerged(), bgfx::Access::Read );
        bgfx::setImage( 1, m_meteredLuminance, 0, bgfx::Access::Write, bgfx::TextureFormat::R32F );
        bgfx::dispatch( meterView, m_exposureHistogramProgram->Handle, 1, 1, 1 );
        OutLuminance = m_meteredLuminance;
        return true;
    }


    bgfx::TextureHandle PostProcess::RenderAutoExposure( ViewAllocator& InViews, const PostProcessSettings& InSettings, FrameBuffer& InTargets )
    {
        OPTICK_EVENT( "PostProcess::AutoExposure" );
        if( !InTargets.EnsureAutoExposureTargets() )
        {
            return m_whiteTexture;
        }
        // The average luminance, from the histogram where compute runs, else the log-average chain.
        bgfx::TextureHandle averageLuminance = BGFX_INVALID_HANDLE;
        if( !( UseComputeMetering && !m_histogramMeteringDisabled && RenderHistogramLuminance( InViews, InSettings, InTargets, averageLuminance ) ) )
        {
            const bgfx::ViewId luminanceView = InViews.Allocate( "Luminance" );
            if( luminanceView == UINT16_MAX )
            {
                return m_whiteTexture;
            }
            SetupFullscreenView( luminanceView, InTargets.LuminanceBuffer.Buffer, 1, 1 );
            bgfx::setTexture( 0, s_input0, InTargets.PostInput );
            SubmitFullscreen( luminanceView, m_luminanceProgram );
            averageLuminance = InTargets.LuminanceBuffer.Color;
        }
        const bgfx::ViewId adaptView = InViews.Allocate( "Eye Adaptation" );
        if( adaptView == UINT16_MAX )
        {
            return m_whiteTexture;
        }

        FrameBuffer::Mip& previous = InTargets.AdaptedLuminance[InTargets.AdaptedIndex];
        FrameBuffer::Mip& next = InTargets.AdaptedLuminance[1 - InTargets.AdaptedIndex];
        SetupFullscreenView( adaptView, next.Buffer, 1, 1 );
        // EV100 range -> luminance range (L = 2^EV * 12.5 / 100).
        const float minLuminance = std::pow( 2.f, InSettings.AutoExposureMinEV ) * 0.125f;
        const float maxLuminance = std::pow( 2.f, InSettings.AutoExposureMaxEV ) * 0.125f;
        const float adapt[4] = { 1.f - std::exp( -DeltaSeconds * InSettings.AutoExposureSpeed ), minLuminance, maxLuminance, 0.f };
        bgfx::setUniform( u_adapt, adapt );
        bgfx::setTexture( 0, s_input0, averageLuminance );
        bgfx::setTexture( 1, s_input1, previous.Color );
        SubmitFullscreen( adaptView, m_adaptProgram );
        InTargets.AdaptedIndex = 1 - InTargets.AdaptedIndex;
        return next.Color;
    }


    void ComputeRenderSize( uint32_t InOutputWidth, uint32_t InOutputHeight, float InScale, UpscaleFilter InFilter, float InMaxScale, uint64_t InMaxPixels, uint32_t& OutWidth, uint32_t& OutHeight, uint32_t& OutPixelFactor )
    {
        const uint32_t outputWidth = std::max<uint32_t>( InOutputWidth, 1 );
        const uint32_t outputHeight = std::max<uint32_t>( InOutputHeight, 1 );
        float scale = std::clamp( InScale > 0.f ? InScale : 1.f, 0.05f, 1.f );
        if( InMaxScale > 0.f )
        {
            scale = std::min( scale, InMaxScale );
        }
        const double outputPixels = double( outputWidth ) * double( outputHeight );
        if( InMaxPixels > 0 && outputPixels * scale * scale > double( InMaxPixels ) )
        {
            scale = static_cast<float>( std::sqrt( double( InMaxPixels ) / outputPixels ) );
        }
        OutPixelFactor = 1;
        if( InFilter == UpscaleFilter::Pixel )
        {
            // The nearest whole block size at or above 1 / scale (so the caps still hold).
            OutPixelFactor = std::max<uint32_t>( 1, static_cast<uint32_t>( std::ceil( 1.f / scale - 0.01f ) ) );
            OutWidth = ( outputWidth + OutPixelFactor - 1 ) / OutPixelFactor;
            OutHeight = ( outputHeight + OutPixelFactor - 1 ) / OutPixelFactor;
            return;
        }
        OutWidth = std::clamp<uint32_t>( static_cast<uint32_t>( std::lround( outputWidth * scale ) ), 1, outputWidth );
        OutHeight = std::clamp<uint32_t>( static_cast<uint32_t>( std::lround( outputHeight * scale ) ), 1, outputHeight );
    }


    void ComputePixelPerfect( uint32_t InOutputWidth, uint32_t InOutputHeight, uint32_t InReferenceWidth, uint32_t InReferenceHeight, uint32_t& OutFactor, uint32_t& OutWidth, uint32_t& OutHeight )
    {
        const uint32_t outputWidth = std::max<uint32_t>( InOutputWidth, 1 );
        const uint32_t outputHeight = std::max<uint32_t>( InOutputHeight, 1 );
        const uint32_t referenceWidth = std::max<uint32_t>( InReferenceWidth, 1 );
        const uint32_t referenceHeight = std::max<uint32_t>( InReferenceHeight, 1 );
        OutFactor = std::max<uint32_t>( 1, std::min( outputWidth / referenceWidth, outputHeight / referenceHeight ) );
        OutWidth = ( outputWidth + OutFactor - 1 ) / OutFactor;
        OutHeight = ( outputHeight + OutFactor - 1 ) / OutFactor;
    }


    Vector3 SnapToTexelGrid( const Vector3& InEye, const Vector3& InRight, const Vector3& InUp, float InTexel )
    {
        if( InTexel <= 0.f )
        {
            return InEye;
        }
        const glm::vec3 eye = InEye.InternalVector;
        const glm::vec3 right = glm::normalize( InRight.InternalVector );
        const glm::vec3 up = glm::normalize( InUp.InternalVector );
        const float x = glm::dot( eye, right );
        const float y = glm::dot( eye, up );
        const float snappedX = std::round( x / InTexel ) * InTexel;
        const float snappedY = std::round( y / InTexel ) * InTexel;
        return Vector3( eye + right * ( snappedX - x ) + up * ( snappedY - y ) );
    }


    float StepDynamicScale( float InScale, float InGpuMs, float InTargetMs, float InMinScale, float InMaxScale )
    {
        const float minScale = std::clamp( std::min( InMinScale, InMaxScale ), 0.05f, 1.f );
        const float maxScale = std::clamp( InMaxScale, minScale, 1.f );
        float scale = InScale;
        if( InGpuMs > InTargetMs * 1.05f )
        {
            // Pixel cost is about proportional to the area: aim for the target directly.
            scale *= std::max( std::sqrt( InTargetMs / InGpuMs ), 0.8f );
            scale = std::floor( scale / 0.05f + 0.001f ) * 0.05f;
        }
        else if( InGpuMs < InTargetMs * 0.8f )
        {
            scale += 0.05f;
        }
        return std::clamp( std::round( scale / 0.05f ) * 0.05f, minScale, maxScale );
    }


    void PostProcess::RenderEra( ViewAllocator& InViews, const CameraData& InCamera, FrameBuffer& InTargets, bgfx::FrameBufferHandle InOutput, uint16_t InOutputWidth, uint16_t InOutputHeight, uint16_t InOutputX, uint16_t InOutputY )
    {
        const bgfx::ViewId view = InViews.Allocate( "Era" );
        if( view == UINT16_MAX || !m_eraProgram.IsLoaded() )
        {
            return;
        }
        const PostProcessSettings& settings = InCamera.Post;
        const bool palette = settings.EraPalette && bgfx::isValid( settings.EraPalette->TexHandle );
        const float params[4] = { static_cast<float>( settings.EraKuwahara ), static_cast<float>( settings.EraColorBits ), settings.EraDither,
            palette ? static_cast<float>( std::min( settings.EraPalette->mWidth, 64 ) ) : 0.f };
        SetupFullscreenView( view, InOutput, InOutputWidth, InOutputHeight, InOutputX, InOutputY );
        bgfx::setUniform( u_eraPost, params );
        bgfx::setTexture( 0, s_input0, InTargets.EraInput.Color );
        bgfx::setTexture( 1, s_input1, palette ? settings.EraPalette->TexHandle : m_blackTexture, BGFX_SAMPLER_POINT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP );
        SubmitFullscreen( view, m_eraProgram );
    }


    bool PostProcess::RenderMirror( ViewAllocator& InViews, bgfx::TextureHandle InSource, uint16_t InSourceWidth, uint16_t InSourceHeight, bgfx::FrameBufferHandle InTarget, uint16_t InWidth, uint16_t InHeight )
    {
        if( !bgfx::isValid( InSource ) || !bgfx::isValid( InTarget ) || InSourceWidth == 0 || InSourceHeight == 0 || InWidth == 0 || InHeight == 0 || !m_mirrorProgram.IsLoaded() )
        {
            return false;
        }
        const bgfx::ViewId view = InViews.Allocate( "Mirror" );
        if( view == UINT16_MAX )
        {
            return false;
        }
        // The largest centred crop of the source with the target's shape.
        const float sourceAspect = static_cast<float>( InSourceWidth ) / static_cast<float>( InSourceHeight );
        const float targetAspect = static_cast<float>( InWidth ) / static_cast<float>( InHeight );
        const float cropWidth = sourceAspect > targetAspect ? targetAspect / sourceAspect : 1.f;
        const float cropHeight = sourceAspect > targetAspect ? 1.f : sourceAspect / targetAspect;
        const float rect[4] = { ( 1.f - cropWidth ) * 0.5f, ( 1.f - cropHeight ) * 0.5f, cropWidth, cropHeight };
        // Each tap is bilinear (2 x 2 texels): enough taps per axis to cover an output texel's
        // footprint, up to 4 (8 texels; past that it thins out, like a mip).
        const float texelsPerPixel = cropWidth * static_cast<float>( InSourceWidth ) / static_cast<float>( InWidth );
        const float taps = std::clamp( std::ceil( texelsPerPixel * 0.5f ), 1.f, 4.f );
        const float footprint[4] = { cropWidth / static_cast<float>( InWidth ), cropHeight / static_cast<float>( InHeight ), taps, 0.f };
        SetupFullscreenView( view, InTarget, InWidth, InHeight );
        bgfx::setUniform( u_mirrorRect, rect );
        bgfx::setUniform( u_mirrorTaps, footprint );
        bgfx::setTexture( 0, s_input0, InSource, BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP );
        SubmitFullscreen( view, m_mirrorProgram );
        return true;
    }


    void PostProcess::RenderUpscale( ViewAllocator& InViews, const CameraData& InCamera, FrameBuffer& InTargets, uint32_t InPixelFactor, bgfx::FrameBufferHandle InOutput, uint16_t InOutputWidth, uint16_t InOutputHeight, uint16_t InOutputX, uint16_t InOutputY )
    {
        const bgfx::ViewId view = InViews.Allocate( "Upscale" );
        if( view == UINT16_MAX || !m_upscaleProgram.IsLoaded() )
        {
            return;
        }
        SetupFullscreenView( view, InOutput, InOutputWidth, InOutputHeight, InOutputX, InOutputY );
        const float params[4] = { static_cast<float>( InCamera.Upscale ), InCamera.UpscaleSharpness, static_cast<float>( InPixelFactor ), 0.f };
        const float size[4] = { static_cast<float>( InTargets.UpscaleInput.Width ), static_cast<float>( InTargets.UpscaleInput.Height ), static_cast<float>( InOutputWidth ), static_cast<float>( InOutputHeight ) };
        bgfx::setUniform( u_upscaleParams, params );
        bgfx::setUniform( u_upscaleSize, size );
        bgfx::setTexture( 0, s_input0, InTargets.UpscaleInput.Color );
        SubmitFullscreen( view, m_upscaleProgram );
    }


    void PostProcess::RenderPresent( bgfx::ViewId InView, bgfx::TextureHandle InImage, uint16_t InWidth, uint16_t InHeight )
    {
        // Set up here rather than with SetupFullscreenView: its framebuffer guard would send the
        // back buffer to an incomplete camera's scene buffer.
        float ortho[16];
        bx::mtxOrtho( ortho, 0.f, 1.f, 1.f, 0.f, 0.f, 100.f, 0.f, bgfx::getCaps()->homogeneousDepth );
        bgfx::setViewName( InView, "Present" );
        bgfx::setViewMode( InView, bgfx::ViewMode::Sequential );
        bgfx::setViewFrameBuffer( InView, BGFX_INVALID_HANDLE );
        bgfx::setViewRect( InView, 0, 0, InWidth, InHeight );
        bgfx::setViewTransform( InView, nullptr, ortho );
        bgfx::setTexture( 0, s_input0, InImage, BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP );
        const float unfiltered[4] = { -1.f, 0.f, 0.f, 0.f };
        bgfx::setUniform( u_graphSceneFilter, unfiltered );
        SubmitFullscreen( InView, m_copyProgram );
    }


    void PostProcess::RenderOverlayComposite( ViewAllocator& InViews, const CameraData& InOverlayCamera, const FrameBuffer& InOverlay, bgfx::FrameBufferHandle InOutput, uint16_t InOutputWidth, uint16_t InOutputHeight )
    {
        if( !m_overlayProgram.IsLoaded() || !bgfx::isValid( InOverlay.Texture ) || !bgfx::isValid( InOverlay.DepthTexture ) )
        {
            return;
        }
        const bgfx::ViewId view = InViews.Allocate( "Overlay Composite" );
        if( view == UINT16_MAX )
        {
            return;
        }
        SetupFullscreenView( view, InOutput, InOutputWidth, InOutputHeight );
        // The depth is the scene's, at the internal size.
        // z: 1 when depth is OpenGL-style (-1..1 clip space), w: 1 for an orthographic camera.
        const float params[4] = { 1.f / static_cast<float>( std::max<uint32_t>( InOverlay.Width, 1u ) ), 1.f / static_cast<float>( std::max<uint32_t>( InOverlay.Height, 1u ) ),
            bgfx::getCaps()->homogeneousDepth ? 1.f : 0.f, InOverlayCamera.Projection == ProjectionType::Orthographic ? 1.f : 0.f };
        bgfx::setUniform( u_overlayParams, params );
        // Near, far, fade start, fade end.
        const float fade[4] = { InOverlayCamera.Near, InOverlayCamera.Far, InOverlayCamera.OverlayFade.x, InOverlayCamera.OverlayFade.y };
        bgfx::setUniform( u_overlayFade, fade );
        // Dark opacity, overall opacity, top fade (share of the height), 1 when uv's v runs up the screen.
        const float look[4] = { InOverlayCamera.OverlayDarkOpacity, InOverlayCamera.OverlayOpacity, InOverlayCamera.OverlayTopFade, bgfx::getCaps()->originBottomLeft ? 1.f : 0.f };
        bgfx::setUniform( u_overlayLook, look );
        bgfx::setTexture( 0, s_input0, InOverlay.Texture, BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP );
        bgfx::setTexture( 1, s_input1, InOverlay.DepthTexture, BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP | BGFX_SAMPLER_POINT );
        // Covered: the overlay. Uncovered: the scene plus the overlay (black where it drew nothing).
        SubmitFullscreen( view, m_overlayProgram, BGFX_STATE_WRITE_RGB | BGFX_STATE_BLEND_FUNC( BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_INV_SRC_ALPHA ) );
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
        if( settings.Bloom && settings.BloomIntensity > 0.f )
        {
            InTargets.EnsureBloomTargets();
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
            SetupFullscreenView( tonemapView, InOutput, InOutputWidth, InOutputHeight, InOutputX, InOutputY );
        }

        const float compensation = std::pow( 2.f, settings.ExposureCompensation );
        const float tonemap[4] = { compensation, static_cast<float>( settings.Operator ), bloom ? settings.BloomIntensity : 0.f, settings.VignetteIntensity };
        const float grading[4] = { settings.Saturation, settings.Contrast, settings.Temperature, settings.Tint };
        const float grading2[4] = { settings.VignetteSmoothness, settings.Gamma, settings.Lift, settings.Gain };
        const float grading3[4] = { std::clamp( settings.SaturationKeepRed, 0.f, 1.f ), std::clamp( settings.SaturationKeepBlue, 0.f, 1.f ), std::clamp( settings.SaturationKeepWarmth, 0.f, 1.f ), 0.f };
        const float exposure[4] = { settings.AutoExposure ? 1.f : 0.f, compensation, fxaa ? 1.f : 0.f, InCamera.PixelPerfect ? 0.f : 1.f };
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
            else if( settings.LutStrip && bgfx::isValid( settings.LutStrip->TexHandle ) && !settings.LutStrip->IsRenderTarget()
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
        bgfx::setUniform( u_grading3, grading3 );
        bgfx::setUniform( u_exposure, exposure );
        bgfx::setUniform( u_lut, lut );
        // The grain's pattern: the camera's, else a new one each pass. Kept small so the shader's
        // hash stays exact in float.
        const uint32_t grainFrame = settings.FilmGrainFrame >= 0 ? static_cast<uint32_t>( settings.FilmGrainFrame ) : m_grainCounter++;
        const float grain[4] = { settings.FilmGrain, std::max( settings.FilmGrainSize, 1.f ), static_cast<float>( grainFrame % 4096u ), 0.f };
        bgfx::setUniform( u_grain, grain );
        const float lens[4] = { PostProcessSettings::LensDistortionAngle( settings.LensDistortion ), settings.LensFringe, static_cast<float>( InTargets.Width ) / static_cast<float>( std::max<uint32_t>( InTargets.Height, 1u ) ), 0.f };
        bgfx::setUniform( u_lens, lens );
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
