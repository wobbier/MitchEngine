#include "ShaderGraphMaterial.h"
#include "Path.h"
#include "Resource/ResourceCache.h"
#include "bgfx/defines.h"
#include <cstring>

#if USING( ME_TOOLS )
#include "Events/EditorEvents.h"
#include "Utils/HavanaUtils.h"
#include "Utils/ImGuiUtils.h"
#include <imgui.h>
#endif

namespace
{
    uint64_t Mix( uint64_t h, uint64_t v )
    {
        return h ^ ( v + 0x9e3779b97f4a7c15ULL + ( h << 6 ) + ( h >> 2 ) );
    }
}


ShaderGraphMaterial::ShaderGraphMaterial()
    : Moonlight::Material( "ShaderGraphMaterial", "" )
{
    SupportsInstancing = true;
}


void ShaderGraphMaterial::Init()
{
    Moonlight::ShaderGraphAsset::CreateUniforms();
}


void ShaderGraphMaterial::SetGraph( const std::string& InGraphPath )
{
    m_graphPath = InGraphPath;
    m_asset.reset();
    MeshShader = Moonlight::ShaderCommand();
    m_syncedRevision = 0;
    if( !InGraphPath.empty() )
    {
        m_asset = ResourceCache::GetInstance().Get<Moonlight::ShaderGraphAsset>( Moonlight::ShaderGraphAsset::ManifestPathFor( InGraphPath ) );
    }
    m_params.Asset = m_asset;
    SyncWithAsset();
}


void ShaderGraphMaterial::LoadShader( const std::string& InShaderName )
{
    // The mesh inspector's shader picker hands over the asset path without its extension.
    if( InShaderName.empty() )
    {
        return;
    }
    const std::string extension = ".shadergraph";
    const bool hasExtension = InShaderName.size() > extension.size() && InShaderName.compare( InShaderName.size() - extension.size(), extension.size(), extension ) == 0;
    SetGraph( hasExtension ? InShaderName : InShaderName + extension );
}


// Runs on render job threads (PrepareForDraw): only touches this material and reads the asset.
void ShaderGraphMaterial::SyncWithAsset()
{
    if( !m_asset )
    {
        return;
    }
    if( m_syncedRevision == m_asset->GetRevision() && MeshShader.IsLoaded() )
    {
        return;
    }
    m_syncedRevision = m_asset->GetRevision();
    if( const Moonlight::ShaderCommand* forward = m_asset->GetProgram( "Forward" ) )
    {
        MeshShader = *forward;
    }
    ShaderName = m_graphPath;
    RenderMode = m_asset->IsTransparent() ? Moonlight::RenderingMode::Transparent : Moonlight::RenderingMode::Opaque;
    const std::string& blend = m_asset->GetBlend();
    AlphaBlendMode = blend == "Additive" ? Moonlight::BlendMode::Additive
        : blend == "Premultiply" ? Moonlight::BlendMode::Premultiply
        : blend == "Multiply" ? Moonlight::BlendMode::Multiply
        : Moonlight::BlendMode::Alpha;
    const std::string& faces = m_asset->GetRenderFace();
    FaceMode = faces == "Both" ? Moonlight::RenderFaceMode::Both : faces == "Back" ? Moonlight::RenderFaceMode::Back : Moonlight::RenderFaceMode::Front;

    m_asset->PackParameters( m_params.Values, m_block );
    m_blockHash = 0;
    for( const Vector4& value : m_block )
    {
        uint32_t bits[4];
        std::memcpy( bits, &value.x, sizeof( bits ) );
        for( const uint32_t component : bits )
        {
            m_blockHash = Mix( m_blockHash, component );
        }
    }
}


void ShaderGraphMaterial::PrepareForDraw()
{
    SyncWithAsset();
}


void ShaderGraphMaterial::Use()
{
    if( m_asset )
    {
        m_asset->Bind( m_block, m_params.GetTextures() );
    }
}


SharedPtr<Moonlight::Material> ShaderGraphMaterial::CreateInstance()
{
    return MakeShared<ShaderGraphMaterial>( *this );
}


uint64_t ShaderGraphMaterial::GetRenderState( uint64_t InState ) const
{
    uint64_t state = Material::GetRenderState( InState );
    // Transparent graphs don't write depth (they're sorted back to front instead).
    if( m_asset && m_asset->IsTransparent() )
    {
        state &= ~BGFX_STATE_WRITE_Z;
    }
    return state;
}


uint64_t ShaderGraphMaterial::GetInstanceBatchKey() const
{
    uint64_t h = Material::GetInstanceBatchKey();
    h = Mix( h, m_blockHash );
    if( m_asset )
    {
        h = Mix( h, std::hash<std::string>{}( m_graphPath ) );
        for( const auto& texture : m_params.GetTextures() )
        {
            h = Mix( h, texture.second && bgfx::isValid( texture.second->TexHandle ) ? texture.second->TexHandle.idx : 0xFFFFu );
        }
    }
    return h;
}


float ShaderGraphMaterial::GetAlphaCutoff() const
{
    // The cutoff lives in the graph; report "alpha tested" so shadow casters batch per material.
    return m_asset && m_asset->UsesAlphaClip() ? 0.5f : 0.f;
}


bgfx::ProgramHandle ShaderGraphMaterial::GetSkinnedProgram()
{
    return m_asset ? m_asset->GetProgramHandle( "Skinned" ) : bgfx::ProgramHandle( BGFX_INVALID_HANDLE );
}


bgfx::ProgramHandle ShaderGraphMaterial::GetShadowProgram( bool InSkinned )
{
    return m_asset ? m_asset->GetProgramHandle( InSkinned ? "ShadowSkinned" : "Shadow" ) : bgfx::ProgramHandle( BGFX_INVALID_HANDLE );
}


bool ShaderGraphMaterial::CastsShadows() const
{
    return !m_asset || m_asset->CastsShadows();
}


void ShaderGraphMaterial::SetParameter( const std::string& InName, const Vector4& InValue )
{
    m_params.Set( InName, InValue );
    m_syncedRevision = 0;
    SyncWithAsset();
}


Vector4 ShaderGraphMaterial::GetParameter( const std::string& InName ) const
{
    return m_params.Get( InName );
}


void ShaderGraphMaterial::ResetParameter( const std::string& InName )
{
    m_params.Reset( InName );
    m_syncedRevision = 0;
    SyncWithAsset();
}


void ShaderGraphMaterial::SetTextureOverride( const std::string& InName, const std::string& InPath )
{
    // Loaded here (main thread), never in SyncWithAsset (render jobs).
    m_params.SetTexture( InName, InPath );
    m_syncedRevision = 0;
    SyncWithAsset();
}


void ShaderGraphMaterial::OnSerialize( json& OutJson )
{
    OutJson["Type"] = GetTypeName();
    OutJson["Graph"] = m_graphPath;
    json params;
    m_params.ToJson( params );
    OutJson["Params"] = params["Params"];
    OutJson["Textures"] = params["Textures"];
}


void ShaderGraphMaterial::OnDeserialize( const json& InJson )
{
    m_params.FromJson( InJson );
    SetGraph( InJson.value( "Graph", std::string() ) );
}


#if USING( ME_TOOLS )
void ShaderGraphMaterial::OnEditorInspect()
{
    const float width = HavanaUtils::Label( "Graph" );
    const std::string label = m_graphPath.empty() ? std::string( "(none)" ) : m_graphPath;
    if( ImGui::Button( ( label + "##ShaderGraphPicker" ).c_str(), ImVec2( width, 0.f ) ) )
    {
        RequestAssetSelectionEvent evt( [this]( Path InSelected ) {
            SetGraph( InSelected.GetLocalPathString() );
        }, AssetType::ShaderGraph );
        evt.Fire();
    }
    if( m_graphPath.empty() )
    {
        return;
    }
    if( ImGui::Button( "Open in Shader Graph", ImVec2( HavanaUtils::SkipLabel(), 0.f ) ) )
    {
        OpenShaderGraphEvent evt;
        evt.GraphPath = m_graphPath;
        evt.Fire();
    }
    if( !m_asset || !m_asset->IsValid() )
    {
        ImGui::TextDisabled( "Not compiled yet: open and save the graph." );
        return;
    }

    HavanaUtils::Label( "Properties" );
    if( m_params.OnEditorInspect() )
    {
        m_syncedRevision = 0;
        SyncWithAsset();
    }
}
#endif
