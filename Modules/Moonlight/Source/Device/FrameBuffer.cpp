#include "FrameBuffer.h"
#include <CLog.h>
#include "Mathf.h"
#include <algorithm>

namespace
{
    constexpr uint64_t kClampLinear = BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
    constexpr int kMaxBloomMips = 6;

    void DestroyIfValid( bgfx::FrameBufferHandle& handle )
    {
        if( bgfx::isValid( handle ) )
        {
            bgfx::destroy( handle );
        }
        handle = BGFX_INVALID_HANDLE;
    }

    void DestroyIfValid( bgfx::TextureHandle& handle )
    {
        if( bgfx::isValid( handle ) )
        {
            bgfx::destroy( handle );
        }
        handle = BGFX_INVALID_HANDLE;
    }

    Moonlight::FrameBuffer::Mip CreateColorTarget( uint16_t width, uint16_t height, bgfx::TextureFormat::Enum format, const char* name )
    {
        Moonlight::FrameBuffer::Mip mip;
        mip.Width = std::max<uint16_t>( width, 1 );
        mip.Height = std::max<uint16_t>( height, 1 );
        mip.Color = bgfx::createTexture2D( mip.Width, mip.Height, false, 1, format, BGFX_TEXTURE_RT | kClampLinear );
        bgfx::setName( mip.Color, name );
        mip.Buffer = bgfx::createFrameBuffer( 1, &mip.Color, false );
        return mip;
    }
}


Moonlight::FrameBuffer::FrameBuffer( uint32_t width, uint32_t height )
    : Width( width )
    , Height( height )
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
    DestroyIfValid( LuminanceBuffer.Buffer );
    DestroyIfValid( LuminanceBuffer.Color );
    for( Mip& adapted : AdaptedLuminance )
    {
        DestroyIfValid( adapted.Buffer );
        DestroyIfValid( adapted.Color );
    }
    DestroyIfValid( ParticleBuffer );
    DestroyIfValid( ClusterGrid );
    DestroyIfValid( ClusterIndices );
    DestroyIfValid( Texture );
    DestroyIfValid( DepthTexture );
    DestroyIfValid( SceneColor );
    DestroyIfValid( PostColor );
}


void Moonlight::FrameBuffer::Resize( Vector2 newSize )
{
    Width = static_cast<uint32_t>( Mathf::Max( newSize.x, 1.f ) );
    Height = static_cast<uint32_t>( Mathf::Max( newSize.y, 1.f ) );
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
    SceneBuffer = bgfx::createFrameBuffer( 2, sceneAttachments, false );

    PostColor = bgfx::createTexture2D( width, height, false, 1, bgfx::TextureFormat::BGRA8, BGFX_TEXTURE_RT | kClampLinear );
    bgfx::setName( PostColor, "Camera Post LDR" );
    PostBuffer = bgfx::createFrameBuffer( 1, &PostColor, false );
    ParticleBuffer = bgfx::createFrameBuffer( 1, &SceneColor, false );

    Texture = bgfx::createTexture2D( width, height, false, 1, bgfx::TextureFormat::BGRA8, BGFX_TEXTURE_RT | kClampLinear );
    bgfx::setName( Texture, "Camera Output" );
    const bgfx::TextureHandle outputAttachments[] = { Texture, DepthTexture };
    Buffer = bgfx::createFrameBuffer( 2, outputAttachments, false );

    // Bloom: successive half-resolution mips starting at half size.
    uint16_t mipWidth = width / 2;
    uint16_t mipHeight = height / 2;
    for( int i = 0; i < kMaxBloomMips && mipWidth >= 8 && mipHeight >= 8; ++i )
    {
        BloomMips.push_back( CreateColorTarget( mipWidth, mipHeight, bgfx::TextureFormat::RGBA16F, "Bloom Mip" ) );
        mipWidth /= 2;
        mipHeight /= 2;
    }

    AOBuffer = CreateColorTarget( width / 2, height / 2, bgfx::TextureFormat::R8, "SSAO" );
    AOBlurBuffer = CreateColorTarget( width / 2, height / 2, bgfx::TextureFormat::R8, "SSAO Blurred" );

    LuminanceBuffer = CreateColorTarget( 1, 1, bgfx::TextureFormat::R16F, "Average Luminance" );
    for( Mip& adapted : AdaptedLuminance )
    {
        // Start adapted to mid grey so the first frames aren't flashed.
        const float initial = 0.18f;
        adapted.Width = adapted.Height = 1;
        adapted.Color = bgfx::createTexture2D( 1, 1, false, 1, bgfx::TextureFormat::R32F, BGFX_TEXTURE_RT | kClampLinear, bgfx::copy( &initial, sizeof( initial ) ) );
        bgfx::setName( adapted.Color, "Adapted Luminance" );
        adapted.Buffer = bgfx::createFrameBuffer( 1, &adapted.Color, false );
    }
    AdaptedIndex = 0;

    // Sized for ClusterBuilder (16 x 9 tiles, 24 slices; 1024 x 128 light indices).
    ClusterGrid = bgfx::createTexture2D( 16 * 9, 24, false, 1, bgfx::TextureFormat::RGBA32F, BGFX_SAMPLER_POINT | kClampLinear );
    bgfx::setName( ClusterGrid, "Cluster Grid" );
    ClusterIndices = bgfx::createTexture2D( 1024, 128, false, 1, bgfx::TextureFormat::R32F, BGFX_SAMPLER_POINT | kClampLinear );
    bgfx::setName( ClusterIndices, "Cluster Light Indices" );
}
