#include "PCH.h"
#include "GPUDriver.h"

#include "GPUContext.h"

GPUDriverBGFX::GPUDriverBGFX( GPUContext* inContext )
    : Context( inContext )
{
}

GPUDriverBGFX::~GPUDriverBGFX()
{

}

void GPUDriverBGFX::CreateTexture( uint32_t texture_id, ultralight::Ref<ultralight::Bitmap> bitmap )
{

}

void GPUDriverBGFX::UpdateTexture( uint32_t texture_id, ultralight::Ref<ultralight::Bitmap> bitmap )
{

}

void GPUDriverBGFX::BindTexture( uint8_t texture_unit, uint32_t texture_id )
{

}

void GPUDriverBGFX::DestroyTexture( uint32_t texture_id )
{

}

void GPUDriverBGFX::CreateRenderBuffer( uint32_t render_buffer_id, const ultralight::RenderBuffer& buffer )
{

}

void GPUDriverBGFX::BindRenderBuffer( uint32_t render_buffer_id )
{

}

void GPUDriverBGFX::ClearRenderBuffer( uint32_t render_buffer_id )
{

}

void GPUDriverBGFX::DestroyRenderBuffer( uint32_t render_buffer_id )
{

}

void GPUDriverBGFX::CreateGeometry( uint32_t geometry_id, const ultralight::VertexBuffer& vertices, const ultralight::IndexBuffer& indices )
{

}

void GPUDriverBGFX::UpdateGeometry( uint32_t geometry_id, const ultralight::VertexBuffer& vertices, const ultralight::IndexBuffer& indices )
{

}

void GPUDriverBGFX::DrawGeometry( uint32_t geometry_id, uint32_t indices_count, uint32_t indices_offset, const ultralight::GPUState& state )
{

}

void GPUDriverBGFX::DestroyGeometry( uint32_t geometry_id )
{

}

void GPUDriverBGFX::UpdateCommandList( const ultralight::CommandList& list )
{

}

const char* GPUDriverBGFX::name()
{
    return "GPUDriverBGFX";
}

void GPUDriverBGFX::BeginDrawing()
{

}

void GPUDriverBGFX::EndDrawing()
{

}

void GPUDriverBGFX::LoadShaders()
{

}

void GPUDriverBGFX::BindShader( uint8_t shader )
{

}

void GPUDriverBGFX::BindVertexLayout( ultralight::VertexBufferFormat format )
{

}

void GPUDriverBGFX::BindGeometry( uint32_t id )
{

}

void GPUDriverBGFX::SetViewport( uint32_t width, uint32_t height )
{

}

void GPUDriverBGFX::UpdateConstantBuffer( const ultralight::GPUState& state )
{

}

ultralight::Matrix GPUDriverBGFX::ApplyProjection( const ultralight::Matrix4x4& transform, float screen_width, float screen_height )
{
    ultralight::Matrix transform_mat;
    transform_mat.Set( transform );

    ultralight::Matrix result;
    result.SetOrthographicProjection( screen_width, screen_height, false );
    result.Transform( transform_mat );

    return result;
}
