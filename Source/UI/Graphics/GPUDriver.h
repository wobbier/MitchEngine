#include <Ultralight/platform/GPUDriver.h>

#include <map>
#include <vector>
#include <memory>
#include "UI/GPUDriverImpl.h"
#include <Graphics/ShaderStructures.h>
/*

class GPUContext;

namespace Moonlight {
    struct ShaderProgram;
}

// Error, tried to create a View but GPUDriver was NULL and the CPU renderer was disabled. 
// Please call Platform::set_gpu_driver before creating any Views.
class GPUDriverBGFX
    : public ultralight::GPUDriverImpl
{
public:
    GPUDriverBGFX( GPUContext* inContext );

    virtual ~GPUDriverBGFX();

    virtual void CreateTexture( uint32_t texture_id, ultralight::Ref<ultralight::Bitmap> bitmap ) final;
    virtual void UpdateTexture( uint32_t texture_id, ultralight::Ref<ultralight::Bitmap> bitmap ) final;
    virtual void BindTexture( uint8_t texture_unit, uint32_t texture_id ) final;
    virtual void DestroyTexture( uint32_t texture_id ) final;

    virtual void CreateRenderBuffer( uint32_t render_buffer_id, const ultralight::RenderBuffer& buffer ) final;
    virtual void BindRenderBuffer( uint32_t render_buffer_id ) final;
    virtual void ClearRenderBuffer( uint32_t render_buffer_id ) final;
    virtual void DestroyRenderBuffer( uint32_t render_buffer_id ) final;

    virtual void CreateGeometry( uint32_t geometry_id, const ultralight::VertexBuffer& vertices, const ultralight::IndexBuffer& indices ) final;
    virtual void UpdateGeometry( uint32_t geometry_id, const ultralight::VertexBuffer& vertices, const ultralight::IndexBuffer& indices ) final;
    virtual void DrawGeometry( uint32_t geometry_id, uint32_t indices_count, uint32_t indices_offset, const ultralight::GPUState& state ) final;
    virtual void DestroyGeometry( uint32_t geometry_id ) final;

    virtual void UpdateCommandList( const ultralight::CommandList& list ) final;
    virtual bool HasCommandsPending() final {
        return !command_list_.empty();
    }

    const char* name() final;

    void BeginDrawing() final;
    void EndDrawing() final;

protected:
    void LoadShaders();
    void BindShader( uint8_t shader );
    void BindVertexLayout( ultralight::VertexBufferFormat format );
    void BindGeometry( uint32_t id );
    //ComPtr<ID3D11SamplerState> GetSamplerState();
    //ComPtr<ID3D11Buffer> GetConstantBuffer();
    void SetViewport( uint32_t width, uint32_t height );
    void UpdateConstantBuffer( const ultralight::GPUState& state );
    ultralight::Matrix ApplyProjection( const ultralight::Matrix4x4& transform, float screen_width, float screen_height );

    GPUContext* Context;
    //ComPtr<ID3D11SamplerState> sampler_state_;
    //ComPtr<ID3D11Buffer> constant_buffer_;

    struct GeometryEntry
    {
        ultralight::VertexBufferFormat format;
        //ComPtr<ID3D11Buffer> vertexBuffer;
        //ComPtr<ID3D11Buffer> indexBuffer;
    };
    typedef std::map<uint32_t, GeometryEntry> GeometryMap;
    GeometryMap geometry_;

    struct TextureEntry
    {
//ComPtr<ID3D11Texture2D> texture;
//ComPtr<ID3D11ShaderResourceView> texture_srv;

// These members are only used when MSAA is enabled
        bool is_msaa_render_target = false;
        bool needs_resolve = false;
        //ComPtr<ID3D11Texture2D> resolve_texture;
        //ComPtr<ID3D11ShaderResourceView> resolve_texture_srv;
    };

    typedef std::map<uint32_t, TextureEntry> TextureMap;
    TextureMap textures_;

    struct RenderTargetEntry
    {
//ComPtr<ID3D11RenderTargetView> render_target_view;
        uint32_t render_target_texture_id;
    };

    typedef std::map<uint32_t, RenderTargetEntry> RenderTargetMap;
    RenderTargetMap render_targets_;

    std::map<ultralight::ShaderType, Moonlight::ShaderProgram> shaders_;
};
*/