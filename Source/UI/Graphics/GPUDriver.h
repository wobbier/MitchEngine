#include <Ultralight/platform/GPUDriver.h>
#include "bgfx/bgfx.h"
#include <unordered_map>
#include "Core/Buffer.h"

namespace Moonlight { struct FrameBuffer; }

using namespace ultralight;

class UIDriver
    : public ultralight::GPUDriver
{
    bgfx::ViewId kViewId = 100;
public:
    UIDriver();
    void BeginSynchronize() override;
    void EndSynchronize() override;

    uint32_t NextTextureId() override;

    void CreateTexture( uint32_t texture_id, RefPtr<Bitmap> bitmap ) override;
    void UpdateTexture( uint32_t texture_id, RefPtr<Bitmap> bitmap ) override;
    void DestroyTexture( uint32_t texture_id ) override;


    uint32_t NextRenderBufferId() override;

    void CreateRenderBuffer( uint32_t render_buffer_id, const RenderBuffer& buffer ) override;
    void DestroyRenderBuffer( uint32_t render_buffer_id ) override;


    uint32_t NextGeometryId() override;

    void CreateGeometry( uint32_t geometry_id, const VertexBuffer& vertices, const IndexBuffer& indices ) override;
    void UpdateGeometry( uint32_t geometry_id, const VertexBuffer& vertices, const IndexBuffer& indices ) override;
    void DestroyGeometry( uint32_t geometry_id ) override;

    void UpdateCommandList( const CommandList& list ) override;

private:
    uint32_t m_textureCount = 0;
    std::unordered_map<uint32_t, bgfx::TextureHandle> m_storedTextures;

    // maybe I store unused indices in another vector
    uint32_t m_bufferCount = 0;
    std::unordered_map<uint32_t, bgfx::FrameBufferHandle> m_buffers;

    struct GeometryEntry
    {
        ultralight::VertexBufferFormat format;
        bgfx::VertexBufferHandle m_vbh;
        bgfx::IndexBufferHandle m_ibh;
    };
    uint32_t m_geometryCount = 0;
    std::map<uint32_t, GeometryEntry> m_geometry;

    std::vector<ultralight::Command> m_renderCommands;

    // Uniforms
    bgfx::UniformHandle m_stateUniform;
    bgfx::UniformHandle m_transformUniform;
    bgfx::UniformHandle m_scalar4Uniform;
    bgfx::UniformHandle m_vectorUniform;
    bgfx::UniformHandle m_clipSizeUniform;
    bgfx::UniformHandle m_clipUniform;

    // Shaders
    bgfx::ProgramHandle m_fillPathProgram;
};