#if USING( ME_UI )

#include <Ultralight/platform/GPUDriver.h>
#include "bgfx/bgfx.h"
#include <unordered_map>
#include "Core/Buffer.h"
#include "Math/Matrix4.h"
#include <stack>
#include "Ultralight/Matrix.h"

namespace Moonlight {
    struct FrameBuffer;
}

using namespace ultralight;

class UIDriver
    : public ultralight::GPUDriver
{
    friend class UICore;
    static const constexpr bgfx::ViewId kViewId = 40;
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
    void UpdateConstantBuffer( const ultralight::GPUState& inState, uint32_t geoId );
    void RenderCommandList();

    uint32_t m_textureCount = 1;
    std::stack<uint32_t> m_unusedTextures;
    struct UITexture
    {
        bgfx::TextureHandle Handle;
        uint32_t Width = 1.f;
        uint32_t Height = 1.f;
        bool IsRenderTexture = false;
    };
    std::unordered_map<uint32_t, UITexture> m_storedTextures;

    struct UIBuffer
    {
        bgfx::FrameBufferHandle BufferHandle;
        bgfx::TextureHandle TexHandle;
        uint32_t FrameBufferTexture = 0;
        uint32_t RenderViewId = kViewId;
    };
    std::stack<uint32_t> m_unusedBuffers;
    uint32_t m_bufferCount = 1;
    std::unordered_map<uint32_t, UIBuffer> m_buffers;

    // Uniform State
    struct UIUniform
    {
        glm::vec4 State;
        glm::vec4 m_testVector;
        ultralight::Matrix4x4 Transform;
        glm::vec4 Scalar4[2];
        glm::vec4 Vector[8];
        float ClipSize[4];
        ultralight::Matrix4x4 Clip[8];
    };

    struct GeometryEntry
    {
        ultralight::VertexBufferFormat format;
        bgfx::VertexLayoutHandle m_vbhLayout;
        bgfx::DynamicVertexBufferHandle m_vbh;
        bgfx::DynamicIndexBufferHandle m_ibh;
        void* m_vertexBuffer = nullptr;
        void* m_indexBuffer = nullptr;
        uint32_t m_vertexSize = 0;
        uint32_t m_indexSize = 0;
        UIUniform m_uniform;
    };
    // maybe I store unused indices in another vector
    uint32_t m_geometryCount = 1;
    std::map<uint32_t, GeometryEntry> m_geometry;

    std::vector<ultralight::Command> m_renderCommands;
    ultralight::CommandList m_commandList;

    // Uniforms
    bgfx::UniformHandle m_stateUniform;
    bgfx::UniformHandle m_transformUniform;
    bgfx::UniformHandle m_scalar4Uniform;
    bgfx::UniformHandle m_vectorUniform;
    bgfx::UniformHandle m_clipSizeUniform;
    bgfx::UniformHandle m_clipUniform;

    // Shaders
    bgfx::ProgramHandle m_fillPathProgram;
    bgfx::ProgramHandle m_fillProgram;

    // Textures
    bgfx::UniformHandle s_texture0;
    bgfx::UniformHandle s_texture1;

    struct UIDrawInfo
    {
        uint32_t m_numClearCalls = 0;
        uint32_t m_numDrawFillCalls = 0;
        uint32_t m_numDrawFillPathCalls = 0;
    };
    UIDrawInfo m_uiDrawInfo;

    ultralight::Matrix m_identityMatrix;
};

#endif