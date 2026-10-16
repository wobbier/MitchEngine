#pragma once
#include <map>
#include <string>
#include "Graphics/Material.h"
#include "Graphics/Texture.h"
#include "Materials/ShaderGraphAsset.h"

// A material made in the Shader Graph editor (Lit and Unlit graphs). The graph's compiled
// Material.json provides the programs (instanced, skinned, shadow), render state and the
// parameter/texture layout; this holds the per-material parameter values and texture overrides.
class ShaderGraphMaterial
    : public Moonlight::Material
{
public:
    ShaderGraphMaterial();

    void Init() override;
    void Use() override;
    SharedPtr<Material> CreateInstance() override;
    uint64_t GetRenderState( uint64_t InState ) const override;
    uint64_t GetInstanceBatchKey() const override;
    float GetAlphaCutoff() const override;
    bgfx::ProgramHandle GetSkinnedProgram() override;
    bgfx::ProgramHandle GetShadowProgram( bool InSkinned ) override;
    bool CastsShadows() const override;
    void PrepareForDraw() override;

    void OnSerialize( json& OutJson ) override;
    void OnDeserialize( const json& InJson ) override;
    // Selecting a graph through the mesh inspector's shader picker.
    void LoadShader( const std::string& InShaderName ) override;

#if USING( ME_TOOLS )
    void OnEditorInspect() override;
#endif

    // "Assets/Foo.shadergraph" (empty: no graph).
    void SetGraph( const std::string& InGraphPath );
    const std::string& GetGraph() const
    {
        return m_graphPath;
    }
    const SharedPtr<Moonlight::ShaderGraphAsset>& GetAsset() const
    {
        return m_asset;
    }

    // Per-material overrides, by property name (colours in sRGB, like the inspector shows them).
    void SetParameter( const std::string& InName, const Vector4& InValue );
    Vector4 GetParameter( const std::string& InName ) const;
    void ResetParameter( const std::string& InName );
    void SetTextureOverride( const std::string& InName, const std::string& InPath );

private:
    // Follows recompiles: the material reads the program and render state from the asset.
    void SyncWithAsset();

    std::string m_graphPath;
    SharedPtr<Moonlight::ShaderGraphAsset> m_asset;
    std::map<std::string, Vector4> m_parameters;
    std::map<std::string, std::string> m_texturePaths;
    std::map<std::string, SharedPtr<Moonlight::Texture>> m_textures;
    Vector4 m_block[Moonlight::ShaderGraphAsset::kMaxParameters];
    uint64_t m_blockHash = 0;
    uint32_t m_syncedRevision = 0;
};

ME_REGISTER_MATERIAL_NAME( ShaderGraphMaterial, "Shader Graph" )
