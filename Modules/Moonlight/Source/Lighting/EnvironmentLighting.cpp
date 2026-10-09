#include "EnvironmentLighting.h"
#include "RenderViews.h"
#include "Camera/CameraData.h"
#include "Graphics/DynamicSky.h"
#include "Graphics/SkyBox.h"
#include "Graphics/Material.h"
#include "Graphics/Texture.h"
#include "Primitives/Cube.h"
#include "Utils/BGFXUtils.h"
#include "CLog.h"
#include <bx/math.h>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace Moonlight
{
    namespace
    {
        constexpr uint64_t kCubeSampler = BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP | BGFX_SAMPLER_W_CLAMP;

        uint64_t Mix( uint64_t h, uint64_t v )
        {
            return h ^ ( v + 0x9e3779b97f4a7c15ULL + ( h << 6 ) + ( h >> 2 ) );
        }

        uint64_t Quantize( float InValue, float InSteps )
        {
            return static_cast<uint64_t>( static_cast<int64_t>( std::lround( InValue * InSteps ) ) );
        }

        float SrgbToLinear( float c )
        {
            return c <= 0.04045f ? c / 12.92f : std::pow( ( c + 0.055f ) / 1.055f, 2.4f );
        }

        bgfx::ProgramHandle LoadEnvProgram( const char* InFragment )
        {
            return LoadProgram( "Assets/Shaders/Post/Fullscreen.vert", std::string( "Assets/Shaders/Env/" ) + InFragment + ".frag" );
        }
    }


    void EnvironmentLighting::Init()
    {
        m_supported = bgfx::isTextureValid( 0, true, 1, bgfx::TextureFormat::RGBA16F, BGFX_TEXTURE_RT | kCubeSampler );

        m_captureProgram = LoadEnvProgram( "EnvCapture" );
        m_downsampleProgram = LoadEnvProgram( "EnvDownsample" );
        m_prefilterProgram = LoadEnvProgram( "EnvPrefilter" );
        m_irradianceProgram = LoadEnvProgram( "EnvIrradiance" );
        m_brdfProgram = LoadEnvProgram( "BrdfLut" );
        u_envFace = bgfx::createUniform( "u_envFace", bgfx::UniformType::Vec4 );
        u_envSource = bgfx::createUniform( "u_envSource", bgfx::UniformType::Vec4 );
        u_envColor = bgfx::createUniform( "u_envColor", bgfx::UniformType::Vec4 );
        s_envSource = bgfx::createUniform( "s_envSource", bgfx::UniformType::Sampler );
        s_envPanorama = bgfx::createUniform( "s_envPanorama", bgfx::UniformType::Sampler );

        for( bgfx::ProgramHandle program : { m_captureProgram, m_downsampleProgram, m_prefilterProgram, m_irradianceProgram, m_brdfProgram } )
        {
            m_supported = m_supported && bgfx::isValid( program );
        }
        if( !m_supported )
        {
            YIKES( "Image-based lighting disabled: RGBA16F cube render targets or the environment shaders are unavailable." );
        }

        // Bound whenever no probe is ready yet (black: the hemisphere ambient takes over).
        const uint16_t black[4 * 6] = {};
        m_fallbackCube = bgfx::createTextureCube( 1, false, 1, bgfx::TextureFormat::RGBA16F, kCubeSampler, bgfx::copy( black, sizeof( black ) ) );
        bgfx::setName( m_fallbackCube, "Environment Fallback" );
        m_brdfLut = bgfx::createTexture2D( 128, 128, false, 1, bgfx::TextureFormat::RG16F, BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP );
        bgfx::setName( m_brdfLut, "Environment BRDF" );
    }


    void EnvironmentLighting::Destroy()
    {
        ReleaseFrameBuffers();
        for( Probe& probe : m_probes )
        {
            DestroyProbe( probe );
        }
        for( bgfx::ProgramHandle* program : { &m_captureProgram, &m_downsampleProgram, &m_prefilterProgram, &m_irradianceProgram, &m_brdfProgram } )
        {
            if( bgfx::isValid( *program ) )
            {
                bgfx::destroy( *program );
                *program = BGFX_INVALID_HANDLE;
            }
        }
        for( bgfx::UniformHandle* uniform : { &u_envFace, &u_envSource, &u_envColor, &s_envSource, &s_envPanorama } )
        {
            if( bgfx::isValid( *uniform ) )
            {
                bgfx::destroy( *uniform );
                *uniform = BGFX_INVALID_HANDLE;
            }
        }
        for( bgfx::TextureHandle* texture : { &m_brdfLut, &m_fallbackCube } )
        {
            if( bgfx::isValid( *texture ) )
            {
                bgfx::destroy( *texture );
                *texture = BGFX_INVALID_HANDLE;
            }
        }
        m_brdfReady = false;
    }


    EnvironmentLighting::SourceDesc EnvironmentLighting::DescribeSource( const CameraData& InCamera, DynamicSky* InSky ) const
    {
        SourceDesc source;
        if( InCamera.ClearType == ClearColorType::Procedural && InSky )
        {
            // The sky changes with the time of day; refresh when the sun moves about half a degree.
            source.Type = ProceduralSource;
            const bx::Vec3& sun = InSky->m_sun.m_sunDir;
            source.Key = Mix( Mix( Mix( 0x5ca1ab1eULL, Quantize( sun.x, 120.f ) ), Quantize( sun.y, 120.f ) ), Quantize( sun.z, 120.f ) );
            source.Key = Mix( Mix( source.Key, Quantize( InSky->m_turbidity, 100.f ) ), Quantize( InSky->m_time, 20.f ) );
            return source;
        }
        if( InCamera.ClearType == ClearColorType::Skybox && InCamera.Skybox && InCamera.Skybox->SkyMaterial )
        {
            const Texture* panorama = InCamera.Skybox->SkyMaterial->GetTexture( TextureType::Diffuse );
            if( panorama && bgfx::isValid( panorama->TexHandle ) )
            {
                source.Type = PanoramaSource;
                source.Panorama = panorama->TexHandle;
                source.Key = Mix( 0xba5eba11ULL, panorama->TexHandle.idx );
                return source;
            }
        }
        source.Type = ColorSource;
        source.Color[0] = SrgbToLinear( InCamera.ClearColor.x );
        source.Color[1] = SrgbToLinear( InCamera.ClearColor.y );
        source.Color[2] = SrgbToLinear( InCamera.ClearColor.z );
        source.Key = Mix( Mix( Mix( 0xc010c010ULL, Quantize( InCamera.ClearColor.x, 255.f ) ), Quantize( InCamera.ClearColor.y, 255.f ) ), Quantize( InCamera.ClearColor.z, 255.f ) );
        return source;
    }


    const EnvironmentLighting::Probe* EnvironmentLighting::Prepare( ViewAllocator& InViews, const CameraData& InCamera, DynamicSky* InSky, uint64_t InFrame )
    {
        if( !m_supported )
        {
            return nullptr;
        }
        if( !m_brdfReady )
        {
            m_brdfReady = RenderBrdfLut( InViews );
        }

        const SourceDesc source = DescribeSource( InCamera, InSky );
        Probe* probe = nullptr;
        for( Probe& candidate : m_probes )
        {
            if( candidate.Key == source.Key && candidate.SourceType == source.Type )
            {
                probe = &candidate;
                break;
            }
        }
        if( !probe )
        {
            // A moving sky keeps refreshing its own probe (still usable meanwhile); anything else takes
            // the least recently used one.
            for( Probe& candidate : m_probes )
            {
                if( source.Type == ProceduralSource && candidate.SourceType == ProceduralSource )
                {
                    probe = &candidate;
                    break;
                }
            }
            if( !probe )
            {
                probe = &m_probes[0];
                for( Probe& candidate : m_probes )
                {
                    if( candidate.LastUsedFrame < probe->LastUsedFrame )
                    {
                        probe = &candidate;
                    }
                }
                if( probe->LastUsedFrame == InFrame && InFrame != 0 )
                {
                    return nullptr;   // every probe is lighting another camera this frame
                }
            }
        }
        probe->LastUsedFrame = InFrame;

        // One refresh step per frame across all probes: capture, then filter on a later frame.
        if( m_lastUpdateFrame != InFrame )
        {
            if( probe->Captured )
            {
                if( HasFrameBufferHeadroom( 6 * ( kSpecularMips + 1 ) ) && Filter( *probe, InViews ) )
                {
                    probe->Captured = false;
                    probe->Ready = true;
                    m_lastUpdateFrame = InFrame;
                }
            }
            else if( probe->Key != source.Key || probe->SourceType != source.Type )
            {
                if( probe->SourceType != source.Type )
                {
                    probe->Ready = false;
                }
                if( HasFrameBufferHeadroom( 6 * kSourceLevels ) && Capture( *probe, source, InViews, InSky ) )
                {
                    probe->Key = source.Key;
                    probe->SourceType = source.Type;
                    probe->Captured = true;
                    m_lastUpdateFrame = InFrame;
                }
            }
            ReleaseFrameBuffers();
        }
        return probe->Ready && m_brdfReady ? probe : nullptr;
    }


    void EnvironmentLighting::CreateProbe( Probe& OutProbe )
    {
        const uint64_t flags = BGFX_TEXTURE_RT | kCubeSampler;
        for( uint8_t level = 0; level < kSourceLevels; ++level )
        {
            if( !bgfx::isValid( OutProbe.Source[level] ) )
            {
                OutProbe.Source[level] = bgfx::createTextureCube( static_cast<uint16_t>( kCaptureSize >> level ), false, 1, bgfx::TextureFormat::RGBA16F, flags );
                bgfx::setName( OutProbe.Source[level], "Environment Capture" );
            }
        }
        if( !bgfx::isValid( OutProbe.Specular ) )
        {
            OutProbe.Specular = bgfx::createTextureCube( kCaptureSize, true, 1, bgfx::TextureFormat::RGBA16F, flags );
            bgfx::setName( OutProbe.Specular, "Environment Specular" );
        }
        if( !bgfx::isValid( OutProbe.Irradiance ) )
        {
            OutProbe.Irradiance = bgfx::createTextureCube( kIrradianceSize, false, 1, bgfx::TextureFormat::RGBA16F, flags );
            bgfx::setName( OutProbe.Irradiance, "Environment Irradiance" );
        }
    }


    void EnvironmentLighting::DestroyProbe( Probe& InProbe )
    {
        for( bgfx::TextureHandle& texture : InProbe.Source )
        {
            if( bgfx::isValid( texture ) )
            {
                bgfx::destroy( texture );
            }
        }
        for( bgfx::TextureHandle texture : { InProbe.Specular, InProbe.Irradiance } )
        {
            if( bgfx::isValid( texture ) )
            {
                bgfx::destroy( texture );
            }
        }
        InProbe = Probe();
    }


    bool EnvironmentLighting::HasFrameBufferHeadroom( uint32_t InNeeded ) const
    {
        const bgfx::Stats* stats = bgfx::getStats();
        return stats->numFrameBuffers + InNeeded + 8 < bgfx::getCaps()->limits.maxFrameBuffers;
    }


    bgfx::ViewId EnvironmentLighting::BeginPass( ViewAllocator& InViews, const char* InName, bgfx::TextureHandle InTarget, uint16_t InLayer, uint16_t InMip, uint16_t InSize )
    {
        const bgfx::ViewId view = InViews.Allocate( InName );
        if( view == UINT16_MAX )
        {
            return view;
        }
        bgfx::Attachment attachment;
        attachment.init( InTarget, bgfx::Access::Write, InLayer, 1, InMip, BGFX_RESOLVE_NONE );
        const bgfx::FrameBufferHandle frameBuffer = bgfx::createFrameBuffer( 1, &attachment, false );
        m_frameBuffers.push_back( frameBuffer );

        float ortho[16];
        bx::mtxOrtho( ortho, 0.f, 1.f, 1.f, 0.f, 0.f, 100.f, 0.f, bgfx::getCaps()->homogeneousDepth );
        bgfx::setViewFrameBuffer( view, frameBuffer );
        bgfx::setViewRect( view, 0, 0, InSize, InSize );
        bgfx::setViewTransform( view, nullptr, ortho );
        bgfx::touch( view );
        return view;
    }


    void EnvironmentLighting::Submit( bgfx::ViewId InView, bgfx::ProgramHandle InProgram )
    {
        bgfx::setState( BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A );
        screenSpaceQuad( 1.f, 1.f, 0.f, bgfx::getCaps()->originBottomLeft );
        bgfx::submit( InView, InProgram );
    }


    void EnvironmentLighting::ReleaseFrameBuffers()
    {
        // bgfx releases destroyed frame buffers after the frame renders, so this is safe right after
        // the passes using them were submitted.
        for( bgfx::FrameBufferHandle frameBuffer : m_frameBuffers )
        {
            if( bgfx::isValid( frameBuffer ) )
            {
                bgfx::destroy( frameBuffer );
            }
        }
        m_frameBuffers.clear();
    }


    bool EnvironmentLighting::RenderBrdfLut( ViewAllocator& InViews )
    {
        const bgfx::ViewId view = InViews.Allocate( "Environment BRDF" );
        if( view == UINT16_MAX )
        {
            return false;
        }
        const bgfx::FrameBufferHandle frameBuffer = bgfx::createFrameBuffer( 1, &m_brdfLut, false );
        m_frameBuffers.push_back( frameBuffer );
        float ortho[16];
        bx::mtxOrtho( ortho, 0.f, 1.f, 1.f, 0.f, 0.f, 100.f, 0.f, bgfx::getCaps()->homogeneousDepth );
        bgfx::setViewFrameBuffer( view, frameBuffer );
        bgfx::setViewRect( view, 0, 0, 128, 128 );
        bgfx::setViewTransform( view, nullptr, ortho );
        Submit( view, m_brdfProgram );
        ReleaseFrameBuffers();
        return true;
    }


    bool EnvironmentLighting::Capture( Probe& InProbe, const SourceDesc& InSource, ViewAllocator& InViews, DynamicSky* InSky )
    {
        CreateProbe( InProbe );
        const float mode[4] = { static_cast<float>( InSource.Type ), 0.f, 0.f, 0.f };
        for( uint16_t face = 0; face < 6; ++face )
        {
            const bgfx::ViewId view = BeginPass( InViews, "Environment Capture", InProbe.Source[0], face, 0, kCaptureSize );
            if( view == UINT16_MAX )
            {
                return false;
            }
            const float faceParams[4] = { static_cast<float>( face ), 0.f, static_cast<float>( kCaptureSize ), 1.f };
            bgfx::setUniform( u_envFace, faceParams );
            bgfx::setUniform( u_envSource, mode );
            bgfx::setUniform( u_envColor, InSource.Color );
            if( InSource.Type == ProceduralSource && InSky )
            {
                InSky->SetUniforms();
            }
            if( InSource.Type == PanoramaSource )
            {
                bgfx::setTexture( 0, s_envPanorama, InSource.Panorama, BGFX_SAMPLER_V_CLAMP );
            }
            Submit( view, m_captureProgram );
        }

        // Smaller copies for the rougher prefilter levels and the irradiance.
        for( uint8_t level = 1; level < kSourceLevels; ++level )
        {
            const uint16_t size = static_cast<uint16_t>( kCaptureSize >> level );
            for( uint16_t face = 0; face < 6; ++face )
            {
                const bgfx::ViewId view = BeginPass( InViews, "Environment Downsample", InProbe.Source[level], face, 0, size );
                if( view == UINT16_MAX )
                {
                    return false;
                }
                const float faceParams[4] = { static_cast<float>( face ), 0.f, static_cast<float>( size * 2 ), 1.f };
                bgfx::setUniform( u_envFace, faceParams );
                bgfx::setTexture( 0, s_envSource, InProbe.Source[level - 1], kCubeSampler );
                Submit( view, m_downsampleProgram );
            }
        }
        return true;
    }


    bool EnvironmentLighting::Filter( Probe& InProbe, ViewAllocator& InViews )
    {
        for( uint8_t mip = 0; mip < kSpecularMips; ++mip )
        {
            const uint16_t size = static_cast<uint16_t>( kCaptureSize >> mip );
            const float roughness = static_cast<float>( mip ) / static_cast<float>( kSpecularMips - 1 );
            const uint8_t sourceLevel = std::min<uint8_t>( mip, kSourceLevels - 1 );
            const float samples = mip == 0 ? 1.f : ( mip == 1 ? 48.f : 96.f );
            for( uint16_t face = 0; face < 6; ++face )
            {
                const bgfx::ViewId view = BeginPass( InViews, "Environment Prefilter", InProbe.Specular, face, mip, size );
                if( view == UINT16_MAX )
                {
                    return false;
                }
                const float faceParams[4] = { static_cast<float>( face ), roughness, static_cast<float>( kCaptureSize >> sourceLevel ), samples };
                bgfx::setUniform( u_envFace, faceParams );
                bgfx::setTexture( 0, s_envSource, InProbe.Source[sourceLevel], kCubeSampler );
                Submit( view, m_prefilterProgram );
            }
        }
        for( uint16_t face = 0; face < 6; ++face )
        {
            const bgfx::ViewId view = BeginPass( InViews, "Environment Irradiance", InProbe.Irradiance, face, 0, kIrradianceSize );
            if( view == UINT16_MAX )
            {
                return false;
            }
            const float faceParams[4] = { static_cast<float>( face ), 1.f, static_cast<float>( kCaptureSize >> ( kSourceLevels - 1 ) ), 128.f };
            bgfx::setUniform( u_envFace, faceParams );
            bgfx::setTexture( 0, s_envSource, InProbe.Source[kSourceLevels - 1], kCubeSampler );
            Submit( view, m_irradianceProgram );
        }
        return true;
    }
}
