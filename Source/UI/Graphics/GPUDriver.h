#include <Ultralight/platform/GPUDriver.h>
#include "bgfx/bgfx.h"
#include <unordered_map>
#include "Core/Buffer.h"
#include "Math/Matrix4.h"
#include <stack>

namespace Moonlight {
    struct FrameBuffer;
}

using namespace ultralight;

class UIDriver
    : public ultralight::GPUDriver
{
    friend class UICore;
    static const constexpr bgfx::ViewId kViewId = 100;
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
    void UpdateConstantBuffer( const ultralight::GPUState& inState );
    void RenderCommandList();

    uint32_t m_textureCount = 0;
    std::stack<uint32_t> m_unusedTextures;
    struct UITexture
    {
        bgfx::TextureHandle Handle;
        uint32_t Width = 1.f;
        uint32_t Height = 1.f;
        bool IsRenderTexture = false;
    };
    std::unordered_map<uint32_t, UITexture> m_storedTextures;

    // maybe I store unused indices in another vector
    struct UIBuffer
    {
        bgfx::FrameBufferHandle BufferHandle;
        bgfx::TextureHandle TexHandle;
        uint32_t FrameBufferTexture = 0;
        uint32_t RenderViewId = kViewId;
    };
    std::stack<uint32_t> m_unusedBuffers;
    uint32_t m_bufferCount = 0;
    std::unordered_map<uint32_t, UIBuffer> m_buffers;

    struct GeometryEntry
    {
        ultralight::VertexBufferFormat format;
        bgfx::DynamicVertexBufferHandle m_vbh;
        bgfx::DynamicIndexBufferHandle m_ibh;
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

    // Uniform State
    struct UIUniform
    {
        float State[4];
        Matrix4 Transform;
        glm::vec4 Scalar4[2];
        glm::vec4 Vector[8];
        float ClipSize[4];
        Matrix4 Clip[8];
    };
    UIUniform m_uniform;

    // Shaders
    bgfx::ProgramHandle m_fillPathProgram;
    bgfx::ProgramHandle m_fillProgram;

    // Textures
    bgfx::UniformHandle s_texture0;
    bgfx::UniformHandle s_texture1;

};