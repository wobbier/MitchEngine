#pragma once
#include "Dementia.h"

#if USING( ME_RMLUI )

#include <RmlUi/Core/RenderInterface.h>
#include <bgfx/bgfx.h>

#include <cstdint>
#include <vector>

// RmlUi on bgfx: compiled geometry in static buffers, premultiplied-alpha blending, per-draw
// scissor/transform, and stencil clip masks for rounded clipping. Draws into one sequential
// view (RenderView::RmlUi) over the composited frame; layers and filters come later (UI-2+).
class RmlRenderInterface final
    : public Rml::RenderInterface
{
public:
    ~RmlRenderInterface();

    // Creates the layout, program, sampler and white texture. Needs bgfx up; safe to call once.
    // InLoadProgram = false skips the shader program (headless tests on the Noop backend);
    // geometry and texture lifecycles still work, and submits become no-ops.
    bool Init( bool InLoadProgram = true );
    void Shutdown();

    // Points the interface at its view and render target for this frame and resets per-frame
    // state. An invalid target means the backbuffer (tests).
    void BeginFrame( bgfx::ViewId InView, uint16_t InWidth, uint16_t InHeight, bgfx::FrameBufferHandle InTarget = BGFX_INVALID_HANDLE );

    Rml::CompiledGeometryHandle CompileGeometry( Rml::Span<const Rml::Vertex> InVertices, Rml::Span<const int> InIndices ) override;
    void RenderGeometry( Rml::CompiledGeometryHandle InGeometry, Rml::Vector2f InTranslation, Rml::TextureHandle InTexture ) override;
    void ReleaseGeometry( Rml::CompiledGeometryHandle InGeometry ) override;

    Rml::TextureHandle LoadTexture( Rml::Vector2i& OutDimensions, const Rml::String& InSource ) override;
    Rml::TextureHandle GenerateTexture( Rml::Span<const Rml::byte> InSource, Rml::Vector2i InDimensions ) override;
    void ReleaseTexture( Rml::TextureHandle InTexture ) override;

    void EnableScissorRegion( bool InEnable ) override;
    void SetScissorRegion( Rml::Rectanglei InRegion ) override;

    void EnableClipMask( bool InEnable ) override;
    void RenderToClipMask( Rml::ClipMaskOperation InOperation, Rml::CompiledGeometryHandle InGeometry, Rml::Vector2f InTranslation ) override;

    void SetTransform( const Rml::Matrix4f* InTransform ) override;

    // Live handle counts, asserted by the leak doctest.
    int GetLiveGeometryCount() const { return m_liveGeometry; }
    int GetLiveTextureCount() const { return m_liveTextures; }

private:
    struct Geometry
    {
        bgfx::VertexBufferHandle Vertices = BGFX_INVALID_HANDLE;
        bgfx::IndexBufferHandle Indices = BGFX_INVALID_HANDLE;
        uint32_t IndexCount = 0;
    };

    void SubmitGeometry( const Geometry& InGeometry, Rml::Vector2f InTranslation, bgfx::TextureHandle InTexture, uint64_t InState, uint32_t InStencil );
    void ApplyTransform( Rml::Vector2f InTranslation );

    std::vector<Geometry> m_geometry;
    std::vector<uint32_t> m_freeGeometry;
    std::vector<bgfx::TextureHandle> m_textures;
    std::vector<uint32_t> m_freeTextures;

    bgfx::VertexLayout m_layout;
    bgfx::ProgramHandle m_program = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_textureSampler = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle m_whiteTexture = BGFX_INVALID_HANDLE;

    bgfx::ViewId m_view = 0;
    uint16_t m_width = 0;
    uint16_t m_height = 0;

    bool m_scissorEnabled = false;
    Rml::Rectanglei m_scissor = Rml::Rectanglei::MakeInvalid();
    bool m_clipMaskEnabled = false;
    uint8_t m_stencilTestValue = 0;
    uint8_t m_stencilNextValue = 0;
    bool m_haveTransform = false;
    float m_transform[16] = {};
    bool m_warnedInverseClip = false;

    int m_liveGeometry = 0;
    int m_liveTextures = 0;
    bool m_initialized = false;
};

#endif
