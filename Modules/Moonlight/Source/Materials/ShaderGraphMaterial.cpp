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


    json VectorToJson( const Vector4& InValue )
    {
        return json::array( { InValue.x, InValue.y, InValue.z, InValue.w } );
    }


    Vector4 VectorFromJson( const json& InJson )
    {
        Vector4 result;
        if( InJson.is_number() )
        {
            result.x = InJson.get<float>();
        }
        else if( InJson.is_array() )
        {
            float* components[4] = { &result.x, &result.y, &result.z, &result.w };
            for( size_t i = 0; i < InJson.size() && i < 4; ++i )
            {
                *components[i] = InJson[i].is_number() ? InJson[i].get<float>() : 0.f;
            }
        }
        return result;
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

    m_asset->PackParameters( m_parameters, m_block );
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
        m_asset->Bind( m_block, m_textures );
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
        for( const auto& texture : m_textures )
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
    m_parameters[InName] = InValue;
    m_syncedRevision = 0;
    SyncWithAsset();
}


Vector4 ShaderGraphMaterial::GetParameter( const std::string& InName ) const
{
    auto found = m_parameters.find( InName );
    if( found != m_parameters.end() )
    {
        return found->second;
    }
    const Moonlight::ShaderGraphAsset::Parameter* parameter = m_asset ? m_asset->FindParameter( InName ) : nullptr;
    return parameter ? parameter->Default : Vector4();
}


void ShaderGraphMaterial::ResetParameter( const std::string& InName )
{
    m_parameters.erase( InName );
    m_syncedRevision = 0;
    SyncWithAsset();
}


void ShaderGraphMaterial::SetTextureOverride( const std::string& InName, const std::string& InPath )
{
    m_textures.erase( InName );
    if( InPath.empty() )
    {
        m_texturePaths.erase( InName );
    }
    else
    {
        // Loaded here (main thread), never in SyncWithAsset (render jobs).
        m_texturePaths[InName] = InPath;
        m_textures[InName] = ResourceCache::GetInstance().GetAsync<Moonlight::Texture>( Path( InPath ) );
    }
    m_syncedRevision = 0;
    SyncWithAsset();
}


void ShaderGraphMaterial::OnSerialize( json& OutJson )
{
    OutJson["Type"] = GetTypeName();
    OutJson["Graph"] = m_graphPath;
    json& parameters = OutJson["Params"];
    parameters = json::object();
    for( const auto& entry : m_parameters )
    {
        parameters[entry.first] = VectorToJson( entry.second );
    }
    json& textures = OutJson["Textures"];
    textures = json::object();
    for( const auto& entry : m_texturePaths )
    {
        textures[entry.first] = entry.second;
    }
}


void ShaderGraphMaterial::OnDeserialize( const json& InJson )
{
    m_parameters.clear();
    m_texturePaths.clear();
    m_textures.clear();
    if( InJson.contains( "Params" ) && InJson["Params"].is_object() )
    {
        for( auto it = InJson["Params"].begin(); it != InJson["Params"].end(); ++it )
        {
            m_parameters[it.key()] = VectorFromJson( it.value() );
        }
    }
    if( InJson.contains( "Textures" ) && InJson["Textures"].is_object() )
    {
        for( auto it = InJson["Textures"].begin(); it != InJson["Textures"].end(); ++it )
        {
            if( it.value().is_string() && !it.value().get<std::string>().empty() )
            {
                m_texturePaths[it.key()] = it.value().get<std::string>();
                m_textures[it.key()] = ResourceCache::GetInstance().GetAsync<Moonlight::Texture>( Path( it.value().get<std::string>() ) );
            }
        }
    }
    SetGraph( InJson.value( "Graph", std::string() ) );
}


#if USING( ME_TOOLS )
void ShaderGraphMaterial::OnEditorInspect()
{
    HavanaUtils::Label( "Graph" );
    const std::string label = m_graphPath.empty() ? std::string( "(none)" ) : m_graphPath;
    if( ImGui::Button( ( label + "##ShaderGraphPicker" ).c_str(), ImVec2( -1.f, 0.f ) ) )
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
    if( ImGui::Button( "Open in Shader Graph", ImVec2( -1.f, 0.f ) ) )
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

    for( const Moonlight::ShaderGraphAsset::Parameter& parameter : m_asset->GetParameters() )
    {
        if( !parameter.Exposed )
        {
            continue;
        }
        ImGui::PushID( parameter.Name.c_str() );
        Vector4 value = GetParameter( parameter.Name );
        bool changed = false;
        HavanaUtils::Label( parameter.Name );
        if( parameter.Type == "Bool" )
        {
            bool enabled = value.x != 0.f;
            if( ImGui::Checkbox( "##Value", &enabled ) )
            {
                value.x = enabled ? 1.f : 0.f;
                changed = true;
            }
        }
        else if( parameter.IsColor && parameter.Components >= 3 )
        {
            const ImGuiColorEditFlags flags = parameter.IsHDR ? ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float : 0;
            changed = parameter.Components == 4 ? ImGui::ColorEdit4( "##Value", &value.x, flags ) : ImGui::ColorEdit3( "##Value", &value.x, flags );
        }
        else if( parameter.Components == 1 && parameter.Min != parameter.Max )
        {
            changed = ImGui::SliderFloat( "##Value", &value.x, parameter.Min, parameter.Max );
        }
        else
        {
            changed = ImGui::DragScalarN( "##Value", ImGuiDataType_Float, &value.x, parameter.Components, 0.01f );
        }
        if( m_parameters.count( parameter.Name ) )
        {
            ImGui::SameLine();
            if( ImGui::SmallButton( "Reset" ) )
            {
                ResetParameter( parameter.Name );
                changed = false;
            }
        }
        if( changed )
        {
            SetParameter( parameter.Name, value );
        }
        ImGui::PopID();
    }

    for( const Moonlight::ShaderGraphAsset::TextureSlot& texture : m_asset->GetTextures() )
    {
        if( !texture.Exposed )
        {
            continue;
        }
        ImGui::PushID( texture.Name.c_str() );
        HavanaUtils::Label( texture.Name );
        auto overridden = m_texturePaths.find( texture.Name );
        const std::string current = overridden != m_texturePaths.end() ? overridden->second : ( texture.Asset.empty() ? std::string( "(default)" ) : texture.Asset );
        if( ImGui::Button( ( current + "##Texture" ).c_str() ) )
        {
            const std::string name = texture.Name;
            RequestAssetSelectionEvent evt( [this, name]( Path InSelected ) {
                SetTextureOverride( name, InSelected.GetLocalPathString() );
            }, AssetType::Texture );
            evt.Fire();
        }
        if( overridden != m_texturePaths.end() )
        {
            ImGui::SameLine();
            if( ImGui::SmallButton( "Reset" ) )
            {
                SetTextureOverride( texture.Name, "" );
            }
        }
        ImGui::PopID();
    }
}
#endif
