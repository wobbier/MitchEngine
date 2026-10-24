#include "PCH.h"
#include "UI/Rml/RmlRenderInterface.h"

#if USING( ME_RMLUI )

#include "CLog.h"
#include "Path.h"
#include "VFS/VFS.h"
#include "Utils/BGFXUtils.h"

#include <bimg/bimg.h>
#include <bimg/decode.h>
#include <bx/allocator.h>
#include <bx/math.h>
#include <algorithm>

namespace
{
    bx::DefaultAllocator s_imageAllocator;

    // Premultiplied alpha over the composited frame.
    constexpr uint64_t kBaseState = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A
        | BGFX_STATE_BLEND_FUNC( BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_INV_SRC_ALPHA );
    constexpr uint64_t kMaskState = 0;   // stencil only: no color, no depth
}

RmlRenderInterface::~RmlRenderInterface()
{
    // Never destroy bgfx handles here: statics die after bgfx::shutdown. The owning core
    // calls Shutdown() while the renderer is still alive.
}


bool RmlRenderInterface::Init( bool InLoadProgram )
{
    if( m_initialized )
    {
        return true;
    }

    m_layout.begin()
        .add( bgfx::Attrib::Position, 2, bgfx::AttribType::Float )
        .add( bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true )
        .add( bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float )
        .end();

    if( InLoadProgram )
    {
        m_program = Moonlight::LoadProgram( "Assets/Shaders/UI/Rml.vert", "Assets/Shaders/UI/Rml.frag" );
        if( !bgfx::isValid( m_program ) )
        {
            YIKES( "[UI] The RmlUi shader program failed to load." );
            return false;
        }
    }

    m_textureSampler = bgfx::createUniform( "s_texture", bgfx::UniformType::Sampler );

    const uint32_t white = 0xFFFFFFFF;
    m_whiteTexture = bgfx::createTexture2D( 1, 1, false, 1, bgfx::TextureFormat::RGBA8,
        BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP, bgfx::copy( &white, sizeof( white ) ) );

    m_initialized = true;
    return true;
}


void RmlRenderInterface::Shutdown()
{
    if( !m_initialized )
    {
        return;
    }
    for( Geometry& geometry : m_geometry )
    {
        if( bgfx::isValid( geometry.Vertices ) )
        {
            bgfx::destroy( geometry.Vertices );
            bgfx::destroy( geometry.Indices );
        }
    }
    m_geometry.clear();
    m_freeGeometry.clear();
    for( bgfx::TextureHandle& texture : m_textures )
    {
        if( bgfx::isValid( texture ) )
        {
            bgfx::destroy( texture );
        }
    }
    m_textures.clear();
    m_freeTextures.clear();
    m_liveGeometry = 0;
    m_liveTextures = 0;

    if( bgfx::isValid( m_whiteTexture ) )
    {
        bgfx::destroy( m_whiteTexture );
        m_whiteTexture = BGFX_INVALID_HANDLE;
    }
    if( bgfx::isValid( m_textureSampler ) )
    {
        bgfx::destroy( m_textureSampler );
        m_textureSampler = BGFX_INVALID_HANDLE;
    }
    if( bgfx::isValid( m_program ) )
    {
        bgfx::destroy( m_program );
        m_program = BGFX_INVALID_HANDLE;
    }
    m_initialized = false;
}


void RmlRenderInterface::BeginFrame( bgfx::ViewId InView, uint16_t InWidth, uint16_t InHeight, bgfx::FrameBufferHandle InTarget )
{
    m_view = InView;
    m_width = InWidth;
    m_height = InHeight;
    m_stencilTestValue = 0;
    m_stencilNextValue = 0;
    m_clipMaskEnabled = false;
    m_scissorEnabled = false;
    m_haveTransform = false;

    bgfx::setViewName( m_view, "RmlUi" );
    bgfx::setViewRect( m_view, 0, 0, m_width, m_height );
    bgfx::setViewFrameBuffer( m_view, InTarget );
    // Documents paint back-to-front; submission order is the draw order.
    bgfx::setViewMode( m_view, bgfx::ViewMode::Sequential );
    // Into an offscreen target: start from transparent black so the composite only adds the UI.
    const uint16_t clearFlags = bgfx::isValid( InTarget ) ? uint16_t( BGFX_CLEAR_COLOR | BGFX_CLEAR_STENCIL ) : uint16_t( BGFX_CLEAR_STENCIL );
    bgfx::setViewClear( m_view, clearFlags, 0x00000000, 1.f, 0 );

    float proj[16];
    bx::mtxOrtho( proj, 0.f, static_cast<float>( m_width ), static_cast<float>( m_height ), 0.f,
        0.f, 1000.f, 0.f, bgfx::getCaps()->homogeneousDepth );
    bgfx::setViewTransform( m_view, nullptr, proj );
}


Rml::CompiledGeometryHandle RmlRenderInterface::CompileGeometry( Rml::Span<const Rml::Vertex> InVertices, Rml::Span<const int> InIndices )
{
    Geometry geometry;
    geometry.Vertices = bgfx::createVertexBuffer(
        bgfx::copy( InVertices.data(), static_cast<uint32_t>( InVertices.size() * sizeof( Rml::Vertex ) ) ), m_layout );
    geometry.Indices = bgfx::createIndexBuffer(
        bgfx::copy( InIndices.data(), static_cast<uint32_t>( InIndices.size() * sizeof( int ) ) ), BGFX_BUFFER_INDEX32 );
    geometry.IndexCount = static_cast<uint32_t>( InIndices.size() );
    if( !bgfx::isValid( geometry.Vertices ) || !bgfx::isValid( geometry.Indices ) )
    {
        return 0;
    }

    uint32_t slot;
    if( !m_freeGeometry.empty() )
    {
        slot = m_freeGeometry.back();
        m_freeGeometry.pop_back();
        m_geometry[slot] = geometry;
    }
    else
    {
        slot = static_cast<uint32_t>( m_geometry.size() );
        m_geometry.push_back( geometry );
    }
    ++m_liveGeometry;
    return static_cast<Rml::CompiledGeometryHandle>( slot + 1 );
}


void RmlRenderInterface::ApplyTransform( Rml::Vector2f InTranslation )
{
    float translate[16];
    bx::mtxTranslate( translate, InTranslation.x, InTranslation.y, 0.f );
    if( m_haveTransform )
    {
        float model[16];
        bx::mtxMul( model, translate, m_transform );
        bgfx::setTransform( model );
    }
    else
    {
        bgfx::setTransform( translate );
    }
}


void RmlRenderInterface::SubmitGeometry( const Geometry& InGeometry, Rml::Vector2f InTranslation, bgfx::TextureHandle InTexture, uint64_t InState, uint32_t InStencil )
{
    if( !bgfx::isValid( m_program ) )
    {
        return;
    }
    ApplyTransform( InTranslation );

    if( m_scissorEnabled && !m_haveTransform )
    {
        const int x = std::max( 0, m_scissor.Left() );
        const int y = std::max( 0, m_scissor.Top() );
        const int right = std::min( static_cast<int>( m_width ), m_scissor.Right() );
        const int bottom = std::min( static_cast<int>( m_height ), m_scissor.Bottom() );
        bgfx::setScissor( static_cast<uint16_t>( x ), static_cast<uint16_t>( y ),
            static_cast<uint16_t>( std::max( 0, right - x ) ), static_cast<uint16_t>( std::max( 0, bottom - y ) ) );
    }

    bgfx::setState( InState );
    bgfx::setStencil( InStencil, BGFX_STENCIL_NONE );
    bgfx::setTexture( 0, m_textureSampler, InTexture, BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP );
    bgfx::setVertexBuffer( 0, InGeometry.Vertices );
    bgfx::setIndexBuffer( InGeometry.Indices, 0, InGeometry.IndexCount );
    bgfx::submit( m_view, m_program );
}


void RmlRenderInterface::RenderGeometry( Rml::CompiledGeometryHandle InGeometry, Rml::Vector2f InTranslation, Rml::TextureHandle InTexture )
{
    if( InGeometry == 0 )
    {
        return;
    }
    const Geometry& geometry = m_geometry[static_cast<uint32_t>( InGeometry ) - 1];
    const bgfx::TextureHandle texture = InTexture != 0 ? m_textures[static_cast<uint32_t>( InTexture ) - 1] : m_whiteTexture;

    uint32_t stencil = BGFX_STENCIL_NONE;
    if( m_clipMaskEnabled )
    {
        stencil = BGFX_STENCIL_TEST_EQUAL | BGFX_STENCIL_FUNC_REF( m_stencilTestValue ) | BGFX_STENCIL_FUNC_RMASK( 0xFF )
            | BGFX_STENCIL_OP_FAIL_S_KEEP | BGFX_STENCIL_OP_FAIL_Z_KEEP | BGFX_STENCIL_OP_PASS_Z_KEEP;
    }
    SubmitGeometry( geometry, InTranslation, texture, kBaseState, stencil );
}


void RmlRenderInterface::ReleaseGeometry( Rml::CompiledGeometryHandle InGeometry )
{
    if( InGeometry == 0 )
    {
        return;
    }
    const uint32_t slot = static_cast<uint32_t>( InGeometry ) - 1;
    Geometry& geometry = m_geometry[slot];
    bgfx::destroy( geometry.Vertices );
    bgfx::destroy( geometry.Indices );
    geometry = Geometry();
    m_freeGeometry.push_back( slot );
    --m_liveGeometry;
}


Rml::TextureHandle RmlRenderInterface::LoadTexture( Rml::Vector2i& OutDimensions, const Rml::String& InSource )
{
    const Path resolved( InSource );
    std::vector<uint8_t> bytes;
    if( !resolved.Exists || !VFS::Get().ReadBytes( resolved, bytes ) )
    {
        YIKES( "[UI] Image not found: " + InSource );
        return 0;
    }

    bimg::ImageContainer* image = bimg::imageParse( &s_imageAllocator, bytes.data(), static_cast<uint32_t>( bytes.size() ),
        bimg::TextureFormat::RGBA8 );
    if( image == nullptr )
    {
        YIKES( "[UI] Image failed to decode: " + InSource );
        return 0;
    }

    OutDimensions.x = static_cast<int>( image->m_width );
    OutDimensions.y = static_cast<int>( image->m_height );
    const bgfx::Memory* memory = bgfx::copy( image->m_data, image->m_size );
    bimg::imageFree( image );

    // Decoded images carry straight alpha; RmlUi renders everything premultiplied.
    uint8_t* pixels = memory->data;
    for( uint32_t i = 0; i < memory->size; i += 4 )
    {
        const uint32_t alpha = pixels[i + 3];
        pixels[i + 0] = static_cast<uint8_t>( pixels[i + 0] * alpha / 255 );
        pixels[i + 1] = static_cast<uint8_t>( pixels[i + 1] * alpha / 255 );
        pixels[i + 2] = static_cast<uint8_t>( pixels[i + 2] * alpha / 255 );
    }

    const bgfx::TextureHandle texture = bgfx::createTexture2D(
        static_cast<uint16_t>( OutDimensions.x ), static_cast<uint16_t>( OutDimensions.y ), false, 1,
        bgfx::TextureFormat::RGBA8, BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP, memory );
    if( !bgfx::isValid( texture ) )
    {
        return 0;
    }

    uint32_t slot;
    if( !m_freeTextures.empty() )
    {
        slot = m_freeTextures.back();
        m_freeTextures.pop_back();
        m_textures[slot] = texture;
    }
    else
    {
        slot = static_cast<uint32_t>( m_textures.size() );
        m_textures.push_back( texture );
    }
    ++m_liveTextures;
    return static_cast<Rml::TextureHandle>( slot + 1 );
}


Rml::TextureHandle RmlRenderInterface::GenerateTexture( Rml::Span<const Rml::byte> InSource, Rml::Vector2i InDimensions )
{
    const bgfx::TextureHandle texture = bgfx::createTexture2D(
        static_cast<uint16_t>( InDimensions.x ), static_cast<uint16_t>( InDimensions.y ), false, 1,
        bgfx::TextureFormat::RGBA8, BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP,
        bgfx::copy( InSource.data(), static_cast<uint32_t>( InSource.size() ) ) );
    if( !bgfx::isValid( texture ) )
    {
        return 0;
    }

    uint32_t slot;
    if( !m_freeTextures.empty() )
    {
        slot = m_freeTextures.back();
        m_freeTextures.pop_back();
        m_textures[slot] = texture;
    }
    else
    {
        slot = static_cast<uint32_t>( m_textures.size() );
        m_textures.push_back( texture );
    }
    ++m_liveTextures;
    return static_cast<Rml::TextureHandle>( slot + 1 );
}


void RmlRenderInterface::ReleaseTexture( Rml::TextureHandle InTexture )
{
    if( InTexture == 0 )
    {
        return;
    }
    const uint32_t slot = static_cast<uint32_t>( InTexture ) - 1;
    bgfx::destroy( m_textures[slot] );
    m_textures[slot] = BGFX_INVALID_HANDLE;
    m_freeTextures.push_back( slot );
    --m_liveTextures;
}


void RmlRenderInterface::EnableScissorRegion( bool InEnable )
{
    m_scissorEnabled = InEnable;
}


void RmlRenderInterface::SetScissorRegion( Rml::Rectanglei InRegion )
{
    m_scissor = InRegion;
}


void RmlRenderInterface::EnableClipMask( bool InEnable )
{
    m_clipMaskEnabled = InEnable;
}


void RmlRenderInterface::RenderToClipMask( Rml::ClipMaskOperation InOperation, Rml::CompiledGeometryHandle InGeometry, Rml::Vector2f InTranslation )
{
    if( InGeometry == 0 )
    {
        return;
    }
    const Geometry& geometry = m_geometry[static_cast<uint32_t>( InGeometry ) - 1];

    uint32_t stencil = BGFX_STENCIL_NONE;
    switch( InOperation )
    {
    case Rml::ClipMaskOperation::SetInverse:
        if( !m_warnedInverseClip )
        {
            BRUH( "[UI] Inverse clip masks fall back to regular masks until UI-2." );
            m_warnedInverseClip = true;
        }
        [[fallthrough]];
    case Rml::ClipMaskOperation::Set:
        // A fresh reference value instead of a stencil clear: stale values never equal it.
        m_stencilTestValue = ++m_stencilNextValue;
        stencil = BGFX_STENCIL_TEST_ALWAYS | BGFX_STENCIL_FUNC_REF( m_stencilTestValue ) | BGFX_STENCIL_FUNC_RMASK( 0xFF )
            | BGFX_STENCIL_OP_FAIL_S_KEEP | BGFX_STENCIL_OP_FAIL_Z_KEEP | BGFX_STENCIL_OP_PASS_Z_REPLACE;
        break;
    case Rml::ClipMaskOperation::Intersect:
        // Increment where the mask overlaps the current region; the test value follows.
        stencil = BGFX_STENCIL_TEST_EQUAL | BGFX_STENCIL_FUNC_REF( m_stencilTestValue ) | BGFX_STENCIL_FUNC_RMASK( 0xFF )
            | BGFX_STENCIL_OP_FAIL_S_KEEP | BGFX_STENCIL_OP_FAIL_Z_KEEP | BGFX_STENCIL_OP_PASS_Z_INCR;
        m_stencilTestValue = ++m_stencilNextValue;
        break;
    }

    SubmitGeometry( geometry, InTranslation, m_whiteTexture, kMaskState, stencil );
}


void RmlRenderInterface::SetTransform( const Rml::Matrix4f* InTransform )
{
    m_haveTransform = InTransform != nullptr;
    if( m_haveTransform )
    {
        memcpy( m_transform, InTransform->data(), sizeof( m_transform ) );
    }
}

#endif
