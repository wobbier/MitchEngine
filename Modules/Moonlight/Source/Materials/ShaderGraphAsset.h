#pragma once
#include <map>
#include <string>
#include <vector>
#include "Graphics/DefaultTextures.h"
#include "Graphics/ShaderCommand.h"
#include "Graphics/Texture.h"
#include "JSON.h"
#include "Math/Vector4.h"
#include "Pointers.h"
#include "Resource/Resource.h"

namespace Moonlight
{
    // A compiled shader graph: <Graph>.sg/Material.json (written by the editor's shader graph
    // compiler) plus the programs built from the generated sources next to it. Shared by every
    // material, particle system, decal and post-process pass using the graph; reloads in place when
    // the graph is recompiled.
    class ShaderGraphAsset
        : public Resource
    {
    public:
        // Shared by every graph program (bgfx uniforms are global by name, so the layout is fixed;
        // must match ShaderGraph::kMaxParameters / kMaxTextures).
        static constexpr int kMaxParameters = 16;
        static constexpr int kMaxTextures = 8;

        struct Parameter
        {
            std::string Name;
            std::string Id;
            // "Float", "Vec2", "Vec3", "Vec4", "Bool".
            std::string Type;
            int Slot = 0;
            int Components = 1;
            Vector4 Default;
            bool IsColor = false;
            bool IsHDR = false;
            bool Exposed = true;
            float Min = 0.f;
            float Max = 0.f;
        };

        struct TextureSlot
        {
            std::string Name;
            // Empty for textures that aren't properties.
            std::string Id;
            int Slot = 0;
            bool IsCube = false;
            bool Exposed = false;
            std::string Asset;
            DefaultTexture Fallback = DefaultTexture::White;
            SharedPtr<Texture> Loaded;
        };

        explicit ShaderGraphAsset( const Path& InManifestPath );

        bool Load() override;
        void Reload() override;

        // Increments every (re)load: users holding per-parameter state re-sync when it changes.
        uint32_t GetRevision() const
        {
            return m_revision;
        }
        bool IsValid() const
        {
            return m_valid;
        }

        // "Lit", "Unlit", "Particle", "Decal", "PostProcess".
        const std::string& GetTarget() const
        {
            return m_target;
        }
        // Forward / Skinned / Shadow / ShadowSkinned / Pass ...; null when the graph has no such
        // variant.
        const ShaderCommand* GetProgram( const std::string& InName ) const;
        bgfx::ProgramHandle GetProgramHandle( const std::string& InName ) const;

        bool IsTransparent() const
        {
            return m_transparent;
        }
        const std::string& GetBlend() const
        {
            return m_blend;
        }
        const std::string& GetRenderFace() const
        {
            return m_renderFace;
        }
        bool UsesAlphaClip() const
        {
            return m_alphaClip;
        }
        bool CastsShadows() const
        {
            return m_castShadows;
        }
        const json& GetManifest() const
        {
            return m_manifest;
        }

        const std::vector<Parameter>& GetParameters() const
        {
            return m_parameters;
        }
        const std::vector<TextureSlot>& GetTextures() const
        {
            return m_textures;
        }
        const Parameter* FindParameter( const std::string& InName ) const;
        const TextureSlot* FindTexture( const std::string& InName ) const;

        // The parameter block for these per-material values (by parameter name; missing ones use
        // the defaults). Colours convert from sRGB to linear here.
        void PackParameters( const std::map<std::string, Vector4>& InValues, Vector4 OutBlock[kMaxParameters] ) const;
        // Uploads a packed block and binds every texture slot (overrides by name, then the graph's
        // asset, then its fallback). Call before submitting a draw with one of the programs.
        void Bind( const Vector4 InBlock[kMaxParameters], const std::map<std::string, SharedPtr<Texture>>& InTextures ) const;

        // The graph's project path ("Assets/Foo.shadergraph").
        const std::string& GetGraphPath() const
        {
            return m_graphPath;
        }

        // <Graph>.sg/Material.json for a graph path (accepts the manifest path itself).
        static Path ManifestPathFor( const std::string& InGraphPath );
        static void CreateUniforms();

    private:
        bool Parse();

        json m_manifest;
        std::string m_graphPath;
        std::string m_target = "Lit";
        std::string m_blend = "Alpha";
        std::string m_renderFace = "Front";
        bool m_transparent = false;
        bool m_alphaClip = false;
        bool m_castShadows = true;
        bool m_valid = false;
        uint32_t m_revision = 0;
        std::map<std::string, ShaderCommand> m_programs;
        std::vector<Parameter> m_parameters;
        std::vector<TextureSlot> m_textures;

        inline static bgfx::UniformHandle s_params = BGFX_INVALID_HANDLE;
        inline static bgfx::UniformHandle s_samplers[kMaxTextures] = {
            BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE,
            BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE
        };
    };
}
