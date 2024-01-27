#include "PCH.h"
#include "GPUDriver.h"
#include "Utils\BGFXUtils.h"
#include "bimg\decode.h"
#include "Core\Assert.h"
#include "Device\FrameBuffer.h"
#include "bgfx\bgfx.h"
#include "Graphics\ShaderStructures.h"
#include <DirectXMath.h>
#include <glm/glm.hpp>


UIDriver::UIDriver()
{
    m_stateUniform = bgfx::createUniform( "State", bgfx::UniformType::Vec4 );
    m_testUniform = bgfx::createUniform( "u_testUniform", bgfx::UniformType::Vec4 );
    m_transformUniform = bgfx::createUniform( "Transform", bgfx::UniformType::Mat4 );

    m_scalar4Uniform = bgfx::createUniform( "Scalar4", bgfx::UniformType::Vec4, 2 );//[2]
    m_vectorUniform = bgfx::createUniform( "Vector", bgfx::UniformType::Vec4, 8 );//[8]
    m_clipSizeUniform = bgfx::createUniform( "ClipSize", bgfx::UniformType::Vec4 );//originally just a uint, can I do that?
    m_clipUniform = bgfx::createUniform( "Clip", bgfx::UniformType::Mat4, 8 );//[8]



    // Textures
    s_texture0 = bgfx::createUniform( "s_texture0", bgfx::UniformType::Sampler );
    s_texture1 = bgfx::createUniform( "s_texture1", bgfx::UniformType::Sampler );
    //memset( &m_uniform, 0, sizeof( UIUniform ) );

    // Shaders
    m_fillPathProgram = Moonlight::LoadProgram( "Assets/Shaders/UIFillPath.vert", "Assets/Shaders/UIFillPath.frag" ); // Vertex_2f_4ub_2f
    m_fillProgram = Moonlight::LoadProgram( "Assets/Shaders/UIFill.vert", "Assets/Shaders/UIFill.frag" ); // Vertex_2f_4ub_2f_2f_28f
    {
        uint32_t bufSize = sizeof( Moonlight::Vertex_2f_4ub_2f_2f_28f ) * 4;
        Moonlight::Vertex_2f_4ub_2f_2f_28f* testBuf = static_cast<Moonlight::Vertex_2f_4ub_2f_2f_28f*>( malloc( bufSize ) );
        memset( testBuf, 0, bufSize );
        testBuf[0].data_6[0] = 69;
        testBuf[1].data_6[0] = 69;
        testBuf[2].data_6[0] = 69;
        testBuf[3].data_6[0] = 69;
        m_testVertexBuffer = bgfx::createVertexBuffer( bgfx::makeRef( testBuf, bufSize ), Moonlight::Vertex_2f_4ub_2f_2f_28f::ms_layout );
    }


    {
        uint32_t indSize = sizeof( uint32_t ) * 4;
        uint32_t* testBuf = static_cast<uint32_t*>( malloc( indSize ) );
        memset( testBuf, 0, indSize );
        testBuf[0] = 0;
        testBuf[1] = 1;
        testBuf[2] = 2;
        testBuf[3] = 3;
        m_testIndexBuffer = bgfx::createIndexBuffer( bgfx::makeRef( testBuf, indSize ), BGFX_BUFFER_INDEX32 );
    }
}

void UIDriver::BeginSynchronize()
{
}

void UIDriver::EndSynchronize()
{
}

uint32_t UIDriver::NextTextureId()
{
    if( !m_unusedTextures.empty() )
    {
        uint32_t texture_id = m_unusedTextures.top();
        m_unusedTextures.pop();
        return texture_id;
    }

    return m_textureCount++;
}

void UIDriver::CreateTexture( uint32_t texture_id, RefPtr<Bitmap> bitmap )
{
    if( !( bitmap->format() == BitmapFormat::BGRA8_UNORM_SRGB
        || bitmap->format() == BitmapFormat::A8_UNORM ) ) {
        ME_ASSERT_MSG( nullptr, "GPUDriverD3D11::CreateTexture, unsupported format." );
    }

    auto& newTex = m_storedTextures[texture_id];
    newTex.Width = bitmap->width();
    newTex.Height = bitmap->height();

    if( bitmap->IsEmpty() )
    {
        newTex.IsRenderTexture = true;

        uint32_t resetFlags = 0;
        uint32_t msaa = ( resetFlags & BGFX_RESET_MSAA_MASK ) >> BGFX_RESET_MSAA_SHIFT;

        const uint64_t textureFlags = BGFX_TEXTURE_RT_WRITE_ONLY | ( uint64_t( msaa + 1 ) << BGFX_TEXTURE_RT_MSAA_SHIFT );


        newTex.Handle = bgfx::createTexture2D(
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
        newTex.IsRenderTexture = false;

        void* pixels = bitmap->LockPixels();

        uint32_t width = bitmap->width();
        uint32_t height = bitmap->height();
        uint32_t stride = bitmap->row_bytes();
        bgfx::TextureFormat::Enum format = bitmap->format() == ultralight::BitmapFormat::BGRA8_UNORM_SRGB ? bgfx::TextureFormat::BGRA8 : bgfx::TextureFormat::A8;


        //std::string outputTest = "Assets/TestUI" + std::to_string( texture_id ) + ".png";
        //bitmap->WritePNG( Path( outputTest ).FullPath.c_str() );

        {
            const uint16_t tw = static_cast<uint32_t>( bitmap->width() );
            const uint16_t th = static_cast<uint32_t>( bitmap->height() );
            const uint16_t tx = 0;
            const uint16_t ty = 0;
            uint64_t flags = BGFX_TEXTURE_NONE | BGFX_SAMPLER_W_MIRROR | BGFX_CAPS_FORMAT_TEXTURE_2D;

            const bgfx::Memory* mem = bgfx::makeRef( pixels, width * height * ( format == bgfx::TextureFormat::A8 ? 1 : 4 ) );
            newTex.Handle = bgfx::createTexture2D(
                width
                , height
                , false
                , 1
                , format
                , flags /*BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT | BGFX_SAMPLER_MIP_POINT*/
                , mem );

            //if( bgfx::isValid( m_uiTexture ) && Camera::CurrentCamera )
            //{
            //    bgfx::updateTexture2D( m_uiTexture, 0, 0, tx, ty, tw, th, mem, stride );
            //    GetEngine().GetRenderer().GetCameraCache().Get( Camera::CurrentCamera->GetCameraId() )->UITexture = m_uiTexture;
            //}
        }

        bitmap->UnlockPixels();
    }
}

void UIDriver::UpdateTexture( uint32_t texture_id, RefPtr<Bitmap> bitmap )
{
    void* pixels = bitmap->LockPixels();

    uint32_t width = bitmap->width();
    uint32_t height = bitmap->height();
    uint32_t stride = bitmap->row_bytes();
    m_storedTextures[texture_id].Width = width;
    m_storedTextures[texture_id].Height = height;

    {
        const uint16_t tw = static_cast<uint32_t>( bitmap->width() );
        const uint16_t th = static_cast<uint32_t>( bitmap->height() );
        const uint16_t tx = 0;
        const uint16_t ty = 0;

        const bgfx::Memory* mem = bgfx::makeRef( pixels, width * height * 4 );
        bgfx::updateTexture2D( m_storedTextures[texture_id].Handle, 0, 0, tx, ty, tw, th, mem, stride );
    }

    bitmap->UnlockPixels();
}

void UIDriver::DestroyTexture( uint32_t texture_id )
{
    bgfx::destroy( m_storedTextures[texture_id].Handle );
    m_storedTextures.erase( texture_id );
    m_unusedTextures.push( texture_id );
}

uint32_t UIDriver::NextRenderBufferId()
{
    if( !m_unusedBuffers.empty() )
    {
        uint32_t buffer_id = m_unusedBuffers.top();
        m_unusedBuffers.pop();
        return buffer_id;
    }

    return m_bufferCount++;
}

void UIDriver::CreateRenderBuffer( uint32_t render_buffer_id, const RenderBuffer& buffer )
{
    auto it = m_buffers.find( render_buffer_id );
    ME_ASSERT_MSG( it == m_buffers.end(), "Render Buffer already exists." );

    bgfx::TextureHandle fbtextures[] =
    {
        m_storedTextures[buffer.texture_id].Handle
    };
    // do I need to destroy textures here?
    m_buffers[render_buffer_id].BufferHandle = bgfx::createFrameBuffer( BX_COUNTOF( fbtextures ), fbtextures, true );
    m_buffers[render_buffer_id].TexHandle = m_storedTextures[buffer.texture_id].Handle;
    m_buffers[render_buffer_id].FrameBufferTexture = buffer.texture_id;
    m_buffers[render_buffer_id].RenderViewId = kViewId + render_buffer_id;
}

void UIDriver::DestroyRenderBuffer( uint32_t render_buffer_id )
{
    bgfx::destroy( m_buffers[render_buffer_id].BufferHandle );
    m_buffers.erase( render_buffer_id );
    m_unusedBuffers.push( render_buffer_id );
}

uint32_t UIDriver::NextGeometryId()
{
    return m_geometryCount++;
}

void UIDriver::CreateGeometry( uint32_t geometry_id, const VertexBuffer& vertices, const IndexBuffer& indices )
{
    auto& geo = m_geometry[geometry_id];
    geo.format = vertices.format;
    geo.m_vertexSize = vertices.size;
    geo.m_indexSize = indices.size;
    geo.m_vertexBuffer = malloc( vertices.size );
    geo.m_indexBuffer = malloc( indices.size );
    memcpy( geo.m_vertexBuffer, vertices.data, vertices.size );
    memcpy( geo.m_indexBuffer, indices.data, indices.size );
    // I think this uint16 shit is gonna blow up on me
    if( vertices.format == VertexBufferFormat::_2f_4ub_2f )
    {
        geo.m_vbh = bgfx::createDynamicVertexBuffer(
            bgfx::makeRef( geo.m_vertexBuffer, geo.m_vertexSize )
            , Moonlight::Vertex_2f_4ub_2f::ms_layout, BGFX_BUFFER_ALLOW_RESIZE );
        geo.m_vbhLayout = bgfx::createVertexLayout( Moonlight::Vertex_2f_4ub_2f::ms_layout );
    }
    else if( vertices.format == VertexBufferFormat::_2f_4ub_2f_2f_28f )
    {
        geo.m_vbh = bgfx::createDynamicVertexBuffer(
            bgfx::makeRef( geo.m_vertexBuffer, geo.m_vertexSize )
            , Moonlight::Vertex_2f_4ub_2f_2f_28f::ms_layout, BGFX_BUFFER_ALLOW_RESIZE );
        geo.m_vbhLayout = bgfx::createVertexLayout( Moonlight::Vertex_2f_4ub_2f_2f_28f::ms_layout );
    }

    geo.m_ibh = bgfx::createDynamicIndexBuffer( bgfx::makeRef( geo.m_indexBuffer, geo.m_indexSize )
        , BGFX_BUFFER_ALLOW_RESIZE | BGFX_BUFFER_INDEX32 );
}

void UIDriver::UpdateGeometry( uint32_t geometry_id, const VertexBuffer& vertices, const IndexBuffer& indices )
{
    m_geometry[geometry_id].format = vertices.format;
    auto& geo = m_geometry[geometry_id];
    ME_ASSERT( geo.m_vertexSize == vertices.size );
    ME_ASSERT( geo.m_indexSize == indices.size );
    memcpy( geo.m_vertexBuffer, vertices.data, vertices.size );
    memcpy( geo.m_indexBuffer, indices.data, indices.size );
    geo.m_vertexSize = vertices.size;
    geo.m_indexSize = indices.size;
    // I think this uint16 shit is gonna blow up on me
    bgfx::update( geo.m_vbh, 0, bgfx::makeRef( geo.m_vertexBuffer, geo.m_vertexSize ) );
    bgfx::update( geo.m_ibh, 0, bgfx::makeRef( geo.m_indexBuffer, geo.m_indexSize ) );
}

void UIDriver::DestroyGeometry( uint32_t geometry_id )
{
    bgfx::destroy( m_geometry[geometry_id].m_ibh );
    bgfx::destroy( m_geometry[geometry_id].m_vbh );
    delete m_geometry[geometry_id].m_vertexBuffer;
    delete m_geometry[geometry_id].m_indexBuffer;
    m_geometry.erase( geometry_id );
}

void UIDriver::UpdateCommandList( const CommandList& list )
{
    if( list.size > 0 )
    {
        m_renderCommands.resize( list.size );
        memcpy( &m_renderCommands[0], list.commands, sizeof( ultralight::Command ) * list.size );
    }

}

ultralight::Matrix ApplyProjection( const Matrix4x4& transform,
    float screen_width,
    float screen_height ) {
    Matrix transform_mat;
    transform_mat.Set( transform );

    Matrix result;
    result.SetOrthographicProjection( screen_width, screen_height, false );
    result.Transform( transform_mat );

    return result;
}

void UIDriver::UpdateConstantBuffer( const ultralight::GPUState& inState, uint32_t geoId )
{
    //bgfx_set_state( state, 0 );
    float screen_width = (float)inState.viewport_width;
    float screen_height = (float)inState.viewport_height;
    //glm::mat4 pos( 1.f );
    //pos = glm::translate( pos, { screen_width / 2.0f, screen_height / 2.0f, 0.0f } );
    //pos = glm::scale( pos, { screen_width / 2.0f, screen_height / 2.0f, 1.0f } );
    //pos = glm::rotate( pos, glm::pi<float>(), {0.f, 0.f, 1.f});
    //pos = glm::rotate( pos, glm::pi<float>(), { 0.f, 1.f, 0.f } );

    ME_ASSERT( sizeof( ultralight::Vertex_2f_4ub_2f_2f_28f ) == sizeof( Moonlight::Vertex_2f_4ub_2f_2f_28f ) );
    ME_ASSERT( sizeof( ultralight::Vertex_2f_4ub_2f ) == sizeof( Moonlight::Vertex_2f_4ub_2f ) );

    UIUniform& m_uniform = m_geometry[geoId].m_uniform;
    
    //ultralight::Matrix transform = inState.transform.SetOrthographicProjection( command.gpu_state.viewport_width, command.gpu_state.viewport_height, true );
    m_uniform.Transform = ApplyProjection( inState.transform, screen_width, screen_height ).GetMatrix4x4();
    //m_uniform.Transform.Set( 0, 0, 0, 0
    //    , 0, 0, 0, 0
    //    , 0, 0, 0, 0
    //    , 0, 0, 0, 0 );
    //m_uniform.Transform.SetIdentity();
    m_identityMatrix.SetIdentity();
    bgfx::setTransform( &m_identityMatrix.data, 1);

    m_uniform.State.x = 0.f;
    m_uniform.State.y = screen_width;
    m_uniform.State.z = screen_height;
    m_uniform.State.w = 1.f;

    m_uniform.Scalar4[0] = glm::vec4( inState.uniform_scalar[0], inState.uniform_scalar[1], inState.uniform_scalar[2],
        inState.uniform_scalar[3] );
    m_uniform.Scalar4[1] = { inState.uniform_scalar[4], inState.uniform_scalar[5], inState.uniform_scalar[6],
                            inState.uniform_scalar[7] };

    for( size_t i = 0; i < 8; ++i )
    {
        //m_uniform.Vector[i] = inState.uniform_vector[i].x;
        //m_uniform.Vector[i] = inState.uniform_vector[i].y;
        //m_uniform.Vector[i] = inState.uniform_vector[i].z;
        //m_uniform.Vector[i] = inState.uniform_vector[i].w;
        m_uniform.Vector[i] = glm::vec4( inState.uniform_vector[i].x, inState.uniform_vector[i].y,
            inState.uniform_vector[i].z, inState.uniform_vector[i].w );
    }

    m_uniform.ClipSize[0] = inState.clip_size;
    for( size_t i = 0; i < inState.clip_size; ++i )
    {
        m_uniform.Clip[i] = inState.clip[i];
    }
    bgfx::setUniform( m_stateUniform, &m_uniform.State );
    bgfx::setUniform( m_transformUniform, &m_uniform.Transform.data );
    bgfx::setUniform( m_scalar4Uniform, &m_uniform.Scalar4 );
    bgfx::setUniform( m_vectorUniform, &m_uniform.Vector, UINT16_MAX );
    bgfx::setUniform( m_clipSizeUniform, &m_uniform.ClipSize, UINT16_MAX );
    bgfx::setUniform( m_clipUniform, &m_uniform.Clip, UINT16_MAX );
    m_uniform.m_testVector.x = 69.f;
    bgfx::setUniform( m_testUniform, &m_uniform.m_testVector.x );
}

void UIDriver::RenderCommandList()
{
    memset( &m_uiDrawInfo, 0, sizeof(UIDrawInfo) );
    // Move this
    for( ultralight::Command& command : m_renderCommands )
    {
        if( command.command_type == CommandType::ClearRenderBuffer )
        {
            uint32_t color = (uint32_t)( 0.f * 255.f ) << 24  // Alpha channel (0.5 opacity)
                | (uint32_t)( 0.f * 255.f ) << 16      // Red channel
                | (uint32_t)( 0.f * 255.f ) << 8       // Green channel
                | (uint32_t)( 0.f * 255.f );           // Blue channel
            
            bgfx::touch( kViewId + command.gpu_state.render_buffer_id );
            bgfx::setViewClear( kViewId + command.gpu_state.render_buffer_id
                , BGFX_CLEAR_COLOR
                , color, 1.0f, 0
            );
            //bgfx::setViewClear( kViewId + command.gpu_state.render_buffer_id, BGFX_CLEAR_COLOR );
            m_uiDrawInfo.m_numClearCalls++;
            bgfx::setViewMode( kViewId + command.gpu_state.render_buffer_id, bgfx::ViewMode::Sequential );
            continue;
        }
        {
            UIBuffer& view = m_buffers[command.gpu_state.render_buffer_id];
            bgfx::ViewId bufferId = view.RenderViewId;
            std::string viewName = "UI " + std::to_string( bufferId );
            bgfx::setViewName( bufferId, viewName.c_str() );

            bgfx::setViewRect( bufferId, 0, 0, command.gpu_state.viewport_width, command.gpu_state.viewport_height );

            bgfx::setViewFrameBuffer( bufferId, m_buffers[command.gpu_state.render_buffer_id].BufferHandle );
            // Set vertex and index buffer.
            auto& geo = m_geometry[command.geometry_id];
           // uint32_t vertCount = geo.format == VertexBufferFormat::_2f_4ub_2f ? geo.m_vertexSize / sizeof( Moonlight::Vertex_2f_4ub_2f ) : geo.m_vertexSize / sizeof( Moonlight::Vertex_2f_4ub_2f_2f_28f );
            bgfx::setVertexBuffer( 0, geo.m_vbh );// , 0, vertCount, geo.m_vbhLayout );//  );
            bgfx::setIndexBuffer( geo.m_ibh, command.indices_offset, command.indices_count );

            if( bgfx::isValid( m_storedTextures[command.gpu_state.texture_1_id].Handle ) )
            {
                bgfx::setTexture( 0, s_texture0, m_storedTextures[command.gpu_state.texture_1_id].Handle );
            }

            if( bgfx::isValid( m_storedTextures[command.gpu_state.texture_2_id].Handle ) )
            {
                bgfx::setTexture( 1, s_texture1, m_storedTextures[command.gpu_state.texture_2_id].Handle );
            }

            if( command.gpu_state.enable_scissor )
            {
                bgfx::setScissor( command.gpu_state.scissor_rect.left, command.gpu_state.scissor_rect.top, command.gpu_state.scissor_rect.right, command.gpu_state.scissor_rect.bottom );
            }
            else
            {
                bgfx::setScissor();
            }


            //if( command.gpu_state.shader_type == ShaderType::Fill )
            //{
            //    ME_ASSERT( command.gpu_state.shader_type == ShaderType::Fill && geo.format == VertexBufferFormat::_2f_4ub_2f_2f_28f );
            //}
            //else
            //{
            //    ME_ASSERT( command.gpu_state.shader_type == ShaderType::FillPath && geo.format == VertexBufferFormat::_2f_4ub_2f );
            //}

            UpdateConstantBuffer( command.gpu_state, command.geometry_id );
            
            uint64_t state = 0
                | BGFX_STATE_WRITE_RGB
                | BGFX_STATE_WRITE_A
                //| BGFX_STATE_DEPTH_TEST_NEVER
                ;
            if( command.gpu_state.enable_blend ) {
                state = state | BGFX_STATE_BLEND_EQUATION_ADD;
                state = state | BGFX_STATE_BLEND_FUNC_SEPARATE( BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_INV_SRC_ALPHA, BGFX_STATE_BLEND_INV_DST_ALPHA, BGFX_STATE_BLEND_ONE );
                //state = state | BGFX_STATE_BLEND_ALPHA;
            }
            //if( command.gpu_state.enable_blend ) {
            //    state = state | BGFX_STATE_BLEND_FUNC( BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA );
            //    state = state | BGFX_STATE_BLEND_ADD;
            //    state = state | BGFX_STATE_BLEND_ONE;
            //}
            bgfx::setState( state, 0 );
            // Set uniform values here using GPUDriverD3D11::UpdateConstantBuffer
            //mesh.MeshMaterial->Use();

            // Set render states?
            //bgfx::setState( mesh.MeshMaterial->GetRenderState( state ) );
            if( command.gpu_state.shader_type == ShaderType::Fill )
            {
                m_uiDrawInfo.m_numDrawFillCalls++;
            }
            else
            {
                m_uiDrawInfo.m_numDrawFillPathCalls++;
            }

            // Submit primitive for rendering to view 0.
            bgfx::submit( bufferId, ( command.gpu_state.shader_type == ShaderType::Fill ) ? m_fillProgram : m_fillPathProgram );

            // why is it not fucked if it's dynamic?
            //{
            //    auto& geo2 = m_geometry[command.geometry_id];
            //    bgfx::setVertexBuffer( 0, m_testVertexBuffer, 0, 4 );//  );
            //    bgfx::setIndexBuffer( m_testIndexBuffer, 0, 4 );
            //    bgfx::submit( bufferId, m_fillProgram );
            //}
        }
    }
    m_renderCommands.clear();
}
