#pragma once
#include "Dementia.h"

#if USING( ME_EDITOR )
#include <future>
#include <map>
#include <string>
#include <vector>
#include <bgfx/bgfx.h>
#include "Math/Vector4.h"
#include "Pointers.h"
#include "ShaderGraph/Compiler.h"
#include "imgui.h"

namespace Moonlight { class Texture; }

// The live previews of one shader graph document: a node-preview atlas (one tile per node) and
// the main preview (the graph's result on a sphere). Driven by the preview-mode compile: when only
// constants changed it re-renders with new uniforms; when the code changed it recompiles on a
// worker thread (shaderc) and keeps showing the last good previews meanwhile.
class ShaderGraphPreview
{
public:
    static constexpr uint16_t kAtlasSize = 2048;
    static constexpr uint16_t kTileSize = 128;
    static constexpr uint16_t kMainSize = 384;

    explicit ShaderGraphPreview( const std::string& InGraphPath );
    ~ShaderGraphPreview();

    // A fresh preview-mode compile. InModes: per node, 0 auto, 1 flat, 2 sphere.
    void Update( const ShaderGraph::CompileResult& InPreview, const ShaderGraph::Graph& InGraph );
    // Polls the background compile and renders what changed (once per frame).
    void Render( bgfx::ViewId InTileView, bgfx::ViewId InMainView );

    // The node's tile in the atlas (false: no preview yet).
    bool GetTile( uint32_t InNode, ImVec2& OutUv0, ImVec2& OutUv1 ) const;
    bgfx::TextureHandle GetAtlas() const
    {
        return m_atlasTexture;
    }
    bool GetMain( ImVec2& OutUv0, ImVec2& OutUv1 ) const;
    bgfx::TextureHandle GetMainTexture() const
    {
        return m_mainTexture;
    }
    bool IsCompiling() const
    {
        return m_job.valid();
    }
    bool HasProgram() const
    {
        return bgfx::isValid( m_program );
    }
    // shaderc errors from the last preview compile, attributed to nodes where possible.
    const std::vector<ShaderGraph::Diagnostic>& GetShaderErrors() const
    {
        return m_shaderErrors;
    }
    uint32_t GetProgramCount() const
    {
        return m_programCount;
    }

private:
    // Everything one preview program needs besides its code.
    struct Snapshot
    {
        std::string Fragment;
        std::string Vertex;
        std::string Varyings;
        std::vector<ShaderGraph::PreviewOutput> Outputs;
        std::vector<Vector4> Constants;
        Vector4 Parameters[ShaderGraph::kMaxParameters];
        std::vector<std::pair<int, SharedPtr<Moonlight::Texture>>> Textures;
        std::vector<std::pair<int, bgfx::TextureHandle>> Fallbacks;
        std::map<uint32_t, uint8_t> Modes;
        bool Animated = false;
        bool Particle = false;
    };
    struct JobResult
    {
        bool Success = false;
        std::string Log;
        std::vector<char> Vertex;
        std::vector<char> Fragment;
    };

    void StartCompile();
    void FinishCompile();
    void CreateResources();
    void DrawQuad( bgfx::ViewId InView, float InX, float InY, float InSize, float InTargetSize, float InSelect, float InMode );

    std::string m_workDirectory;
    Snapshot m_latest;
    Snapshot m_active;
    Snapshot m_compiling;
    std::future<JobResult> m_job;
    uint32_t m_generation = 0;
    uint32_t m_programCount = 0;
    std::vector<ShaderGraph::Diagnostic> m_shaderErrors;
    bool m_dirty = true;

    bgfx::ProgramHandle m_program = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle m_atlasTexture = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle m_atlas = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle m_mainTexture = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle m_main = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_preview = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_select = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_params = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_textures[ShaderGraph::kMaxTextures];
    bgfx::UniformHandle s_particle = BGFX_INVALID_HANDLE;
    bgfx::VertexLayout m_layout;

    // Tile per node, stable while the node exists.
    std::map<uint32_t, int> m_tiles;
    std::vector<int> m_freeTiles;
    bool m_mainRendered = false;
};
#endif
