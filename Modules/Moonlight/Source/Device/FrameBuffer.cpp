#include "FrameBuffer.h"
#include <CLog.h>
#include "Utils/BGFXUtils.h"
#include "Mathf.h"
#include <algorithm>

namespace
{
    constexpr uint64_t kClampLinear = BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
    constexpr int kMaxBloomMips = 6;

    void DestroyIfValid( bgfx::FrameBufferHandle& handle )
    {
        if( bgfx::isValid( handle ) && Moonlight::IsGpuAlive() )
        {
            bgfx::destroy( handle );
        }
        handle = BGFX_INVALID_HANDLE;
    }

    void DestroyIfValid( bgfx::TextureHandle& handle )
    {
        if( bgfx::isValid( handle ) && Moonlight::IsGpuAlive() )
        {
            bgfx::destroy( handle );
        }
        handle = BGFX_INVALID_HANDLE;
    }

    Moonlight::FrameBuffer* s_current = nullptr;

    // bgfx hands back an invalid handle when its framebuffer pool is full: say so (once).
    bgfx::FrameBufferHandle CreateChecked( uint8_t InCount, const bgfx::TextureHandle* InAttachments, const char* InName )
    {
        const bgfx::FrameBufferHandle buffer = bgfx::createFrameBuffer( InCount, InAttachments, false );
        static bool s_logged = false;
        if( !bgfx::isValid( buffer ) && !s_logged )
        {
            s_logged = true;
            const bgfx::Stats* stats = bgfx::getStats();
            YIKES( std::string( "Out of framebuffers: couldn't create \"" ) + InName + "\" (" + std::to_string( stats ? stats->numFrameBuffers : 0 ) + " of "
                + std::to_string( bgfx::getCaps()->limits.maxFrameBuffers ) + " in use). Cameras missing targets are skipped; turn off post effects or cameras to fit." );
        }
        return buffer;
    }

    Moonlight::FrameBuffer::Mip CreateColorTarget( uint16_t width, uint16_t height, bgfx::TextureFormat::Enum format, const char* name, bool mips = false )
    {
        Moonlight::FrameBuffer::Mip mip;
        mip.Width = std::max<uint16_t>( width, 1 );
        mip.Height = std::max<uint16_t>( height, 1 );
        mip.Color = bgfx::createTexture2D( mip.Width, mip.Height, mips, 1, format, BGFX_TEXTURE_RT | kClampLinear );
        bgfx::setName( mip.Color, name );
        mip.Buffer = CreateChecked( 1, &mip.Color, name );
        return mip;
    }
}


bool Moonlight::FrameBuffer::IsIncomplete() const
{
    if( !bgfx::isValid( SceneBuffer ) || !bgfx::isValid( PostBuffer ) || !bgfx::isValid( ParticleBuffer ) || ( OwnsOutput && !bgfx::isValid( Buffer ) ) )
    {
        return true;
    }
    // A target whose texture exists but whose framebuffer couldn't be made.
    auto missing = []( const Mip& InMip ) { return bgfx::isValid( InMip.Color ) && !bgfx::isValid( InMip.Buffer ); };
    for( const Mip& mip : BloomMips )
    {
        if( missing( mip ) ) return true;
    }
    for( const Mip* mip : { &AOBuffer, &AOBlurBuffer, &FogBuffer, &FogBlurBuffer, &FogHistory[0], &FogHistory[1], &LuminanceBuffer, &AdaptedLuminance[0], &AdaptedLuminance[1],
             &TemporalHistory[0], &TemporalHistory[1], &DoFCoC, &DoFPrefilter, &DoFRadius[0], &DoFRadius[1], &DoFNear, &DoFFar, &DoFOutput, &DoFSplitOutput,
             &CloudHistory[0], &CloudHistory[1], &GraphHdr[0], &GraphHdr[1], &GraphLdr[0], &GraphLdr[1], &GraphSceneMips, &UpscaleInput, &EraInput } )
    {
        if( missing( *mip ) ) return true;
    }
    return false;
}


void Moonlight::FrameBuffer::SetCurrent( FrameBuffer* InBuffer )
{
    s_current = InBuffer;
}


bgfx::FrameBufferHandle Moonlight::FrameBuffer::GuardTarget( bgfx::FrameBufferHandle InTarget )
{
    if( bgfx::isValid( InTarget ) || !s_current || !s_current->IsIncomplete() || !bgfx::isValid( s_current->SceneBuffer ) )
    {
        return InTarget;
    }
    return s_current->SceneBuffer;
}


Moonlight::FrameBuffer::FrameBuffer( uint32_t width, uint32_t height )
    : Width( width )
    , Height( height )
    , OutputWidth( width )
    , OutputHeight( height )
{
}


Moonlight::FrameBuffer::~FrameBuffer()
{
    Release();
}


bgfx::TextureFormat::Enum Moonlight::FrameBuffer::GetDepthFormat()
{
    // Sampleable depth (SSAO, soft particles); fall back through the common formats.
    const uint64_t flags = BGFX_TEXTURE_RT | kClampLinear;
    for( bgfx::TextureFormat::Enum format : { bgfx::TextureFormat::D32F, bgfx::TextureFormat::D24S8, bgfx::TextureFormat::D24, bgfx::TextureFormat::D16 } )
    {
        if( bgfx::isTextureValid( 0, false, 1, format, flags ) )
        {
            return format;
        }
    }
    return bgfx::TextureFormat::D16;
}


void Moonlight::FrameBuffer::Release()
{
    // Framebuffers are created without owning their textures (the depth is shared).
    DestroyIfValid( Buffer );
    DestroyIfValid( SceneBuffer );
    DestroyIfValid( PostBuffer );
    for( Mip& mip : BloomMips )
    {
        DestroyIfValid( mip.Buffer );
        DestroyIfValid( mip.Color );
    }
    BloomMips.clear();
    DestroyIfValid( AOBuffer.Buffer );
    DestroyIfValid( AOBuffer.Color );
    DestroyIfValid( AOBlurBuffer.Buffer );
    DestroyIfValid( AOBlurBuffer.Color );
    DestroyIfValid( FogBuffer.Buffer );
    DestroyIfValid( FogBuffer.Color );
    DestroyIfValid( FogBlurBuffer.Buffer );
    DestroyIfValid( FogBlurBuffer.Color );
    DestroyIfValid( LuminanceBuffer.Buffer );
    DestroyIfValid( LuminanceBuffer.Color );
    for( Mip& adapted : AdaptedLuminance )
    {
        DestroyIfValid( adapted.Buffer );
        DestroyIfValid( adapted.Color );
    }
    for( Mip& history : TemporalHistory )
    {
        DestroyIfValid( history.Buffer );
        DestroyIfValid( history.Color );
    }
    TemporalValid = false;
    for( Mip* target : { &DoFCoC, &DoFPrefilter, &DoFRadius[0], &DoFRadius[1], &DoFNear, &DoFFar, &DoFOutput, &DoFSplitOutput } )
    {
        DestroyIfValid( target->Buffer );
        DestroyIfValid( target->Color );
    }
    for( Mip& clouds : CloudHistory )
    {
        DestroyIfValid( clouds.Buffer );
        DestroyIfValid( clouds.Color );
    }
    CloudValid = false;
    for( Mip& fog : FogHistory )
    {
        DestroyIfValid( fog.Buffer );
        DestroyIfValid( fog.Color );
    }
    FogHistoryValid = false;
    for( Mip* graph : { &GraphHdr[0], &GraphHdr[1], &GraphLdr[0], &GraphLdr[1], &GraphSceneMips, &UpscaleInput, &EraInput } )
    {
        DestroyIfValid( graph->Buffer );
        DestroyIfValid( graph->Color );
    }
    PostInput = BGFX_INVALID_HANDLE;
    DestroyIfValid( ParticleBuffer );
    DestroyIfValid( ClusterGrid );
    DestroyIfValid( ClusterIndices );
    DestroyIfValid( Texture );
    DestroyIfValid( DepthTexture );
    DestroyIfValid( OutputDepth );
    DestroyIfValid( SceneColor );
    DestroyIfValid( PostColor );
}


void Moonlight::FrameBuffer::EnsureBloomTargets()
{
    if( !BloomMips.empty() )
    {
        return;
    }
    // Successive half-resolution mips starting at half size.
    uint16_t mipWidth = static_cast<uint16_t>( std::max<uint32_t>( Width, 1 ) / 2 );
    uint16_t mipHeight = static_cast<uint16_t>( std::max<uint32_t>( Height, 1 ) / 2 );
    for( int i = 0; i < kMaxBloomMips && mipWidth >= 8 && mipHeight >= 8; ++i )
    {
        BloomMips.push_back( CreateColorTarget( mipWidth, mipHeight, bgfx::TextureFormat::RGBA16F, "Bloom Mip" ) );
        mipWidth /= 2;
        mipHeight /= 2;
    }
}


void Moonlight::FrameBuffer::EnsureAmbientOcclusionTargets()
{
    if( bgfx::isValid( AOBuffer.Color ) )
    {
        return;
    }
    const uint16_t width = static_cast<uint16_t>( std::max<uint32_t>( Width / 2, 1 ) );
    const uint16_t height = static_cast<uint16_t>( std::max<uint32_t>( Height / 2, 1 ) );
    AOBuffer = CreateColorTarget( width, height, bgfx::TextureFormat::R8, "SSAO" );
    AOBlurBuffer = CreateColorTarget( width, height, bgfx::TextureFormat::R8, "SSAO Blurred" );
}


void Moonlight::FrameBuffer::EnsureVolumetricFogTargets()
{
    if( bgfx::isValid( FogBuffer.Color ) )
    {
        return;
    }
    const uint16_t width = static_cast<uint16_t>( std::max<uint32_t>( Width / 2, 1 ) );
    const uint16_t height = static_cast<uint16_t>( std::max<uint32_t>( Height / 2, 1 ) );
    FogBuffer = CreateColorTarget( width, height, bgfx::TextureFormat::RGBA16F, "Volumetric Fog" );
    FogBlurBuffer = CreateColorTarget( width, height, bgfx::TextureFormat::RGBA16F, "Volumetric Fog Blurred" );
}


void Moonlight::FrameBuffer::EnsureTemporalTargets()
{
    for( Mip& history : TemporalHistory )
    {
        if( !bgfx::isValid( history.Color ) )
        {
            history = CreateColorTarget( static_cast<uint16_t>( std::max<uint32_t>( Width, 1 ) ), static_cast<uint16_t>( std::max<uint32_t>( Height, 1 ) ), bgfx::TextureFormat::RGBA16F, "Temporal AA History" );
            TemporalValid = false;
        }
    }
}


void Moonlight::FrameBuffer::EnsureDepthOfFieldTargets( bool InSplit )
{
    const uint16_t width = static_cast<uint16_t>( std::max( Width, 1u ) );
    const uint16_t height = static_cast<uint16_t>( std::max( Height, 1u ) );
    if( InSplit && !bgfx::isValid( DoFSplitOutput.Color ) )
        DoFSplitOutput = CreateColorTarget( width, height, bgfx::TextureFormat::RGBA16F, "DoF Split Composite" );
    if( bgfx::isValid( DoFOutput.Color ) ) return;
    const uint16_t halfWidth = (width + 1u) / 2u;
    const uint16_t halfHeight = (height + 1u) / 2u;
    DoFCoC = CreateColorTarget( width, height, bgfx::TextureFormat::R16F, "DoF Signed CoC" );
    DoFPrefilter = CreateColorTarget( halfWidth, halfHeight, bgfx::TextureFormat::RGBA16F, "DoF Prefilter" );
    for( Mip& radius : DoFRadius ) radius = CreateColorTarget( halfWidth, halfHeight, bgfx::TextureFormat::R16F, "DoF Foreground Radius" );
    DoFNear = CreateColorTarget( halfWidth, halfHeight, bgfx::TextureFormat::RGBA16F, "DoF Near" );
    DoFFar = CreateColorTarget( halfWidth, halfHeight, bgfx::TextureFormat::RGBA16F, "DoF Far" );
    DoFOutput = CreateColorTarget( width, height, bgfx::TextureFormat::RGBA16F, "DoF Composite" );
}


void Moonlight::FrameBuffer::EnsureFogHistoryTargets()
{
    for( Mip& fog : FogHistory )
    {
        if( !bgfx::isValid( fog.Color ) )
        {
            fog = CreateColorTarget( static_cast<uint16_t>( std::max<uint32_t>( Width / 2, 1 ) ), static_cast<uint16_t>( std::max<uint32_t>( Height / 2, 1 ) ), bgfx::TextureFormat::RGBA16F, "Volumetric Fog History" );
            FogHistoryValid = false;
        }
    }
}


void Moonlight::FrameBuffer::EnsureCloudTargets()
{
    for( Mip& clouds : CloudHistory )
    {
        if( !bgfx::isValid( clouds.Color ) )
        {
            clouds = CreateColorTarget( static_cast<uint16_t>( std::max<uint32_t>( Width / 2, 1 ) ), static_cast<uint16_t>( std::max<uint32_t>( Height / 2, 1 ) ), bgfx::TextureFormat::RGBA16F, "Sky Clouds" );
            CloudValid = false;
        }
    }
}


void Moonlight::FrameBuffer::EnsureGraphTargets( bool InHdr, bool InLdr )
{
    const uint16_t width = static_cast<uint16_t>( std::max<uint32_t>( Width, 1 ) );
    const uint16_t height = static_cast<uint16_t>( std::max<uint32_t>( Height, 1 ) );
    for( int i = 0; i < 2; ++i )
    {
        if( InHdr && !bgfx::isValid( GraphHdr[i].Color ) )
        {
            GraphHdr[i] = CreateColorTarget( width, height, bgfx::TextureFormat::RGBA16F, "Shader Graph Post (HDR)" );
        }
        if( InLdr && !bgfx::isValid( GraphLdr[i].Color ) )
        {
            GraphLdr[i] = CreateColorTarget( width, height, bgfx::TextureFormat::RGBA8, "Shader Graph Post (LDR)" );
        }
    }
}


void Moonlight::FrameBuffer::EnsureGraphSceneMips()
{
    if( !bgfx::isValid( GraphSceneMips.Color ) )
    {
        // Hardware mip blits use a 2x2 filter. Odd-sized reductions can skip source texels and
        // make a small bright light pulse, so upsample once to a power-of-two grid first.
        uint32_t width = 1;
        uint32_t height = 1;
        while( width < Width ) width *= 2;
        while( height < Height ) height *= 2;
        GraphSceneMips = CreateColorTarget( static_cast<uint16_t>( width ), static_cast<uint16_t>( height ), bgfx::TextureFormat::RGBA16F, "Shader Graph Scene Mips", true );
    }
}


void Moonlight::FrameBuffer::EnsureUpscaleTarget()
{
    if( !bgfx::isValid( UpscaleInput.Color ) )
    {
        UpscaleInput = CreateColorTarget( static_cast<uint16_t>( std::max<uint32_t>( Width, 1 ) ), static_cast<uint16_t>( std::max<uint32_t>( Height, 1 ) ), bgfx::TextureFormat::BGRA8, "Camera Upscale Input" );
    }
}


void Moonlight::FrameBuffer::EnsureEraTarget()
{
    if( !bgfx::isValid( EraInput.Color ) )
    {
        EraInput = CreateColorTarget( static_cast<uint16_t>( std::max<uint32_t>( Width, 1 ) ), static_cast<uint16_t>( std::max<uint32_t>( Height, 1 ) ), bgfx::TextureFormat::BGRA8, "Camera Era Input" );
    }
}


void Moonlight::FrameBuffer::Resize( Vector2 newSize )
{
    Width = OutputWidth = static_cast<uint32_t>( Mathf::Max( newSize.x, 1.f ) );
    Height = OutputHeight = static_cast<uint32_t>( Mathf::Max( newSize.y, 1.f ) );
    ReCreate( m_resetFlags );
}


void Moonlight::FrameBuffer::ReCreate( uint32_t resetFlags )
{
    m_resetFlags = resetFlags;
    Release();

    const uint16_t width = static_cast<uint16_t>( std::max<uint32_t>( Width, 1 ) );
    const uint16_t height = static_cast<uint16_t>( std::max<uint32_t>( Height, 1 ) );

    DepthTexture = bgfx::createTexture2D( width, height, false, 1, GetDepthFormat(), BGFX_TEXTURE_RT | kClampLinear | BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT );
    bgfx::setName( DepthTexture, "Camera Depth" );

    SceneColor = bgfx::createTexture2D( width, height, false, 1, bgfx::TextureFormat::RGBA16F, BGFX_TEXTURE_RT | kClampLinear );
    bgfx::setName( SceneColor, "Camera HDR Colour" );
    const bgfx::TextureHandle sceneAttachments[] = { SceneColor, DepthTexture };
    SceneBuffer = CreateChecked( 2, sceneAttachments, "Camera Scene" );

    PostColor = bgfx::createTexture2D( width, height, false, 1, bgfx::TextureFormat::BGRA8, BGFX_TEXTURE_RT | kClampLinear );
    bgfx::setName( PostColor, "Camera Post LDR" );
    PostBuffer = CreateChecked( 1, &PostColor, "Camera Post LDR" );
    ParticleBuffer = CreateChecked( 1, &SceneColor, "Camera Particles" );

    if( OwnsOutput )
    {
        const uint16_t outputWidth = static_cast<uint16_t>( std::max<uint32_t>( GetOutputWidth(), 1 ) );
        const uint16_t outputHeight = static_cast<uint16_t>( std::max<uint32_t>( GetOutputHeight(), 1 ) );
        Texture = bgfx::createTexture2D( outputWidth, outputHeight, false, 1, bgfx::TextureFormat::BGRA8, BGFX_TEXTURE_RT | kClampLinear );
        bgfx::setName( Texture, "Camera Output" );
        // Scaled: the scene's depth is the wrong size, so overlays get a depth of their own.
        if( outputWidth != width || outputHeight != height )
        {
            OutputDepth = bgfx::createTexture2D( outputWidth, outputHeight, false, 1, GetDepthFormat(), BGFX_TEXTURE_RT_WRITE_ONLY );
            bgfx::setName( OutputDepth, "Camera Output Depth" );
        }
        const bgfx::TextureHandle outputAttachments[] = { Texture, bgfx::isValid( OutputDepth ) ? OutputDepth : DepthTexture };
        Buffer = CreateChecked( 2, outputAttachments, "Camera Output" );
    }

    LuminanceBuffer = CreateColorTarget( 1, 1, bgfx::TextureFormat::R16F, "Average Luminance" );
    for( Mip& adapted : AdaptedLuminance )
    {
        // Start adapted to mid grey so the first frames aren't flashed.
        const float initial = 0.18f;
        adapted.Width = adapted.Height = 1;
        adapted.Color = bgfx::createTexture2D( 1, 1, false, 1, bgfx::TextureFormat::R32F, BGFX_TEXTURE_RT | kClampLinear, bgfx::copy( &initial, sizeof( initial ) ) );
        bgfx::setName( adapted.Color, "Adapted Luminance" );
        adapted.Buffer = CreateChecked( 1, &adapted.Color, "Adapted Luminance" );
    }
    AdaptedIndex = 0;

    // Sized for ClusterBuilder (16 x 9 tiles, 24 slices; 1024 x 128 light indices).
    ClusterGrid = bgfx::createTexture2D( 16 * 9, 24, false, 1, bgfx::TextureFormat::RGBA32F, BGFX_SAMPLER_POINT | kClampLinear );
    bgfx::setName( ClusterGrid, "Cluster Grid" );
    ClusterIndices = bgfx::createTexture2D( 1024, 128, false, 1, bgfx::TextureFormat::R32F, BGFX_SAMPLER_POINT | kClampLinear );
    bgfx::setName( ClusterIndices, "Cluster Light Indices" );
}
