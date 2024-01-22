#include "PCH.h"
#include "GPUDriver.h"
#include "Utils\BGFXUtils.h"
#include "bimg\decode.h"
#include "Core\Assert.h"
#include "Device\FrameBuffer.h"
#include "bgfx\bgfx.h"
#include "Graphics\ShaderStructures.h"


UIDriver::UIDriver()
{
    m_stateUniform = bgfx::createUniform( "State", bgfx::UniformType::Vec4 );
    m_transformUniform = bgfx::createUniform( "Transform", bgfx::UniformType::Mat4 );

    m_scalar4Uniform = bgfx::createUniform( "Scalar4", bgfx::UniformType::Vec4, 2 );//[2]
    m_vectorUniform = bgfx::createUniform( "Vector", bgfx::UniformType::Vec4, 8 );//[8]
    m_clipSizeUniform = bgfx::createUniform( "ClipSize", bgfx::UniformType::Vec4 );//originally just a uint, can I do that?
    m_clipUniform = bgfx::createUniform( "Clip", bgfx::UniformType::Mat4, 8 );//[8]

    m_fillPathProgram = Moonlight::LoadProgram( "Assets/Shaders/UIFillPath.vert", "Assets/Shaders/UIFillPath.frag" );
    m_fillProgram = Moonlight::LoadProgram( "Assets/Shaders/UIFill.vert", "Assets/Shaders/UIFill.frag" );
}

void UIDriver::BeginSynchronize()
{
}

void UIDriver::EndSynchronize()
{
}

uint32_t UIDriver::NextTextureId()
{
    return m_textureCount++;
}

void UIDriver::CreateTexture( uint32_t texture_id, RefPtr<Bitmap> bitmap )
{
    if( !( bitmap->format() == BitmapFormat::BGRA8_UNORM_SRGB
        || bitmap->format() == BitmapFormat::A8_UNORM ) ) {
        ME_ASSERT_MSG( nullptr, "GPUDriverD3D11::CreateTexture, unsupported format." );
    }

    if( bitmap->IsEmpty() )
    {
        // leaking for now lmao
        //Moonlight::FrameBuffer* mainBuf = new Moonlight::FrameBuffer( bitmap->width(), bitmap->height() );
        //mainBuf->ReCreate(0);
        //m_storedTextures[texture_id] = mainBuf->Texture;
        //m_buffers[texture_id] = mainBuf;


        uint32_t resetFlags = 0;
        uint32_t msaa = ( resetFlags & BGFX_RESET_MSAA_MASK ) >> BGFX_RESET_MSAA_SHIFT;

        const uint64_t textureFlags = BGFX_TEXTURE_RT_WRITE_ONLY | ( uint64_t( msaa + 1 ) << BGFX_TEXTURE_RT_MSAA_SHIFT );
        m_storedTextures[texture_id] = bgfx::createTexture2D(
            bitmap->width()
            , bitmap->height()
            , false
            , 1
            , bgfx::TextureFormat::BGRA8
            , ( uint64_t( msaa + 1 ) << BGFX_TEXTURE_RT_MSAA_SHIFT ) | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP
        );
    }
    else
    {
        if( bimg::ImageContainer* imageContainer = bimg::imageParse( Moonlight::getDefaultAllocator(), bitmap->LockPixels(), bitmap->size() ) )
        {
            const bgfx::Memory* mem = bgfx::makeRef( imageContainer->m_data, imageContainer->m_size, NULL, imageContainer );

    //        BX_FREE( Moonlight::getDefaultAllocator(), (void*)memory );
    //        if( imageContainer->m_cubeMap )
    //        {
    //            YIKES( "You gotta implement cubemap textures" );
    //        }
    //        else if( 1 < imageContainer->m_depth )
    //        {
    //            YIKES( "You gotta implement 3d textures" );
    //        }
    //        else if( bgfx::isTextureValid( 0, false, imageContainer->m_numLayers, bgfx::TextureFormat::Enum( imageContainer->m_format ), flags ) )
    //        {
    //            TexHandle = bgfx::createTexture2D( imageContainer->m_width, imageContainer->m_height, 1 < imageContainer->m_numMips, imageContainer->m_numLayers, bgfx::TextureFormat::Enum( imageContainer->m_format ), flags, mem );
    //        }
    //
    //        if( bgfx::isValid( TexHandle ) )
    //        {
    //            bgfx::setName( TexHandle, FilePath.GetLocalPath().data() );
    //        }
    //
    //        bgfx::TextureInfo* info = nullptr;
    //        if( info )
    //        {
    //            bgfx::calcTextureSize( *info, imageContainer->m_width, imageContainer->m_height, imageContainer->m_depth, imageContainer->m_cubeMap, imageContainer->m_numMips > 0, imageContainer->m_numLayers, bgfx::TextureFormat::Enum( imageContainer->m_format ) );
    //        }
    //        mWidth = imageContainer->m_width;
    //        mHeight = imageContainer->m_height;
    //
    //#if USING ( ME_DEBUG )
    //        BRUH_FMT( "%i, %s", TexHandle.idx, compiledTexture.FullPath.c_str() );
    //#endif
    //        return true;
        }
    }
}

void UIDriver::UpdateTexture( uint32_t texture_id, RefPtr<Bitmap> bitmap )
{
    throw std::logic_error( "The method or operation is not implemented." );
}

void UIDriver::DestroyTexture( uint32_t texture_id )
{
    bgfx::destroy( m_storedTextures[texture_id] );
    m_storedTextures.erase( texture_id );
}

uint32_t UIDriver::NextRenderBufferId()
{
    return m_bufferCount++;
}

void UIDriver::CreateRenderBuffer( uint32_t render_buffer_id, const RenderBuffer& buffer )
{
    auto it = m_buffers.find( render_buffer_id );
    ME_ASSERT_MSG( it == m_buffers.end(), "Render Buffer already exists." );

    bgfx::TextureHandle fbtextures[] =
    {
        m_storedTextures[buffer.texture_id]
    };
    // do I need to destroy textures here?
    m_buffers[render_buffer_id] = bgfx::createFrameBuffer( BX_COUNTOF( fbtextures ), fbtextures, true );
}

void UIDriver::DestroyRenderBuffer( uint32_t render_buffer_id )
{
    bgfx::destroy( m_buffers[render_buffer_id] );
    m_storedTextures.erase( render_buffer_id );
}

uint32_t UIDriver::NextGeometryId()
{
    return m_geometryCount++;
}

void UIDriver::CreateGeometry( uint32_t geometry_id, const VertexBuffer& vertices, const IndexBuffer& indices )
{
    m_geometry[geometry_id].format = vertices.format;
    // I think this uint16 shit is gonna blow up on me
    if( vertices.format == VertexBufferFormat::_2f_4ub_2f )
    {
        m_geometry[geometry_id].m_vbh = bgfx::createVertexBuffer(
            bgfx::makeRef( (void*)vertices.data, vertices.size )
            , Moonlight::Vertex_2f_4ub_2f::ms_layout );
    }
    else if (vertices.format == VertexBufferFormat::_2f_4ub_2f_2f_28f)
    {
        m_geometry[geometry_id].m_vbh = bgfx::createVertexBuffer(
            bgfx::makeRef( (void*)vertices.data, vertices.size )
            , Moonlight::Vertex_2f_4ub_2f_2f_28f::ms_layout );
    }
    m_geometry[geometry_id].m_ibh = bgfx::createIndexBuffer( bgfx::makeRef( indices.data, indices.size ) );
}

void UIDriver::UpdateGeometry( uint32_t geometry_id, const VertexBuffer& vertices, const IndexBuffer& indices )
{
    throw std::logic_error( "The method or operation is not implemented." );
}

void UIDriver::DestroyGeometry( uint32_t geometry_id )
{
    throw std::logic_error( "The method or operation is not implemented." );
}

void UIDriver::UpdateCommandList( const CommandList& list )
{
    if (list.size > 0)
    {
        m_renderCommands.resize( list.size );
        memcpy( &m_renderCommands[0], list.commands, sizeof( ultralight::Command ) * list.size );
    }

    // Move this
    for (auto& command : m_renderCommands)
    {
        switch (command.command_type)
        {
        case CommandType::ClearRenderBuffer:
            bgfx::setViewClear( kViewId, 0 );
            continue;
        case CommandType::DrawGeometry:
            continue;
        }
    }
}
